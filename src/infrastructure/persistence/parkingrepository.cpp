#include "infrastructure/persistence/parkingrepository.h"

#include "domain/parking/parkingfeepolicy.h"
#include "infrastructure/persistence/databasecontext.h"
#include "infrastructure/persistence/vehiclerepository.h"
#include <QDebug>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <algorithm>

namespace {
const QString kDefaultCameraKey = QStringLiteral("camera");
const QString kPendingPayStatus = QStringLiteral("정산대기");
const QString kPaidPayStatus = QStringLiteral("결제완료");

QString normalizedCameraKey(const QString &cameraKey) {
  const QString trimmed = cameraKey.trimmed();
  return trimmed.isEmpty() ? kDefaultCameraKey : trimmed;
}

QString normalizedPlateNumber(const QString &plateNumber) {
  return plateNumber.trimmed();
}

QString normalizedReidId(const QString &reidId) {
  const QString normalized = reidId.trimmed();
  if (normalized == QStringLiteral("V---")) {
    return QString();
  }
  return normalized;
}

bool hasUsableReidId(const QString &reidId) {
  return !normalizedReidId(reidId).isEmpty();
}

QString normalizedPayStatus(const QString &payStatus, bool defaultToPaid) {
  const QString trimmed = payStatus.trimmed();
  if (trimmed.isEmpty()) {
    return defaultToPaid ? kPaidPayStatus : kPendingPayStatus;
  }
  if (trimmed.compare(QStringLiteral("Pending"), Qt::CaseInsensitive) == 0) {
    return kPendingPayStatus;
  }
  if (trimmed.compare(QStringLiteral("Paid"), Qt::CaseInsensitive) == 0) {
    return kPaidPayStatus;
  }
  return trimmed;
}

bool execSql(QSqlDatabase &db, const QString &sql, QString *errorMessage) {
  QSqlQuery query(db);
  if (query.exec(sql)) {
    return true;
  }

  if (errorMessage) {
    *errorMessage = query.lastError().text();
  }
  return false;
}

QVariant nullableIntVariant(int value) {
  return value > 0 ? QVariant(value) : QVariant();
}

QString resolvedPlateExpr() {
  return QStringLiteral(
      "COALESCE(NULLIF(pl.snapshot_plate_number, ''),"
      " (SELECT vp.plate_number FROM vehicle_plates vp "
      "  WHERE vp.vehicle_id = pl.vehicle_id AND vp.is_primary = 1 "
      "  ORDER BY datetime(vp.created_at) DESC, vp.plate_id DESC LIMIT 1),"
      " (SELECT vp.plate_number FROM vehicle_plates vp "
      "  WHERE vp.vehicle_id = pl.vehicle_id "
      "  ORDER BY datetime(vp.created_at) DESC, vp.plate_id DESC LIMIT 1),"
      " '')");
}

QString resolvedReidExpr() {
  return QStringLiteral(
      "COALESCE(NULLIF(pl.snapshot_reid_id, ''), COALESCE(v.current_reid_id, ''), '')");
}

QString resolvedZoneNameExpr() {
  return QStringLiteral(
      "COALESCE(NULLIF(pl.snapshot_zone_name, ''), roi.zone_name, pl.zone_id, '')");
}

QString resolvedCameraExpr() {
  return QStringLiteral(
      "COALESCE(NULLIF(pl.snapshot_camera_key, ''), '%1')")
      .arg(kDefaultCameraKey);
}

QString parkingLogSelectSql() {
  return QStringLiteral(
             "SELECT pl.id, pl.vehicle_id, pl.zone_id, pl.object_id, "
             "pl.entry_time, pl.exit_time, pl.pay_status, pl.total_amount, "
             "pl.created_at, "
             "COALESCE(pl.snapshot_plate_number, '') AS snapshot_plate_number, "
             "COALESCE(pl.snapshot_reid_id, '') AS snapshot_reid_id, "
             "COALESCE(pl.snapshot_camera_key, '') AS snapshot_camera_key, "
             "COALESCE(pl.snapshot_zone_name, '') AS snapshot_zone_name, "
             "%1 AS camera_key, %2 AS zone_name, %3 AS plate_number, %4 AS reid_id "
             "FROM parking_logs pl "
             "LEFT JOIN vehicles v ON v.vehicle_id = pl.vehicle_id "
             "LEFT JOIN roi ON roi.zone_id = pl.zone_id ")
      .arg(resolvedCameraExpr(), resolvedZoneNameExpr(), resolvedPlateExpr(),
           resolvedReidExpr());
}

QJsonObject parkingLogFromQuery(const QSqlQuery &query) {
  QJsonObject record;
  record["id"] = query.value("id").toInt();
  record["vehicle_id"] = query.value("vehicle_id").toInt();
  record["zone_id"] = query.value("zone_id").toString();
  record["camera_key"] = query.value("camera_key").toString();
  record["object_id"] = query.value("object_id").toInt();
  record["plate_number"] = query.value("plate_number").toString();
  record["reid_id"] = query.value("reid_id").toString();
  record["zone_name"] = query.value("zone_name").toString();
  record["entry_time"] = query.value("entry_time").toString();
  record["exit_time"] = query.value("exit_time").toString();
  record["pay_status"] = query.value("pay_status").toString();
  record["total_amount"] = query.value("total_amount").toInt();
  record["created_at"] = query.value("created_at").toString();
  record["snapshot_plate_number"] = query.value("snapshot_plate_number").toString();
  record["snapshot_reid_id"] = query.value("snapshot_reid_id").toString();
  record["snapshot_camera_key"] = query.value("snapshot_camera_key").toString();
  record["snapshot_zone_name"] = query.value("snapshot_zone_name").toString();
  return record;
}

int ensureVehicleId(const QString &plateNumber, const QString &reidId,
                    QString *errorMessage) {
  const QString normalizedPlate = normalizedPlateNumber(plateNumber);
  const QString normalizedReid = normalizedReidId(reidId);
  if (normalizedPlate.isEmpty() && normalizedReid.isEmpty()) {
    return -1;
  }

  VehicleRepository vehicleRepo;
  return vehicleRepo.ensureVehicle(normalizedPlate, QString(), QString(),
                                   normalizedReid, errorMessage);
}

int findActiveRecordIdByObjectId(QSqlDatabase &db, const QString &cameraKey,
                                 int objectId, QString *errorMessage) {
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id FROM parking_logs "
      "WHERE snapshot_camera_key = :camera_key "
      "AND object_id = :object_id AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":object_id", objectId);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return -1;
  }

  return query.next() ? query.value(0).toInt() : -1;
}

int findActiveRecordIdByReid(QSqlDatabase &db, const QString &cameraKey,
                             const QString &reidId, QString *errorMessage) {
  const QString normalizedReid = normalizedReidId(reidId);
  if (normalizedReid.isEmpty()) {
    return -1;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id FROM parking_logs "
      "WHERE snapshot_camera_key = :camera_key "
      "AND snapshot_reid_id = :reid_id AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":reid_id", normalizedReid);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return -1;
  }

  return query.next() ? query.value(0).toInt() : -1;
}
} // namespace

ParkingRepository::ParkingRepository() {}

bool ParkingRepository::init(QString *errorMessage) {
  return ensureSchema(errorMessage);
}

bool ParkingRepository::ensureSchema(QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Database is not open");
    }
    return false;
  }

  if (!execSql(
          db, QStringLiteral("CREATE TABLE IF NOT EXISTS parking_logs ("
                             "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "  vehicle_id INTEGER,"
                             "  zone_id TEXT,"
                             "  object_id INTEGER NOT NULL DEFAULT -1,"
                             "  entry_time TEXT NOT NULL,"
                             "  exit_time TEXT,"
                             "  pay_status TEXT NOT NULL DEFAULT '정산대기',"
                             "  total_amount INTEGER NOT NULL DEFAULT 0,"
                             "  snapshot_plate_number TEXT,"
                             "  snapshot_reid_id TEXT,"
                             "  snapshot_camera_key TEXT NOT NULL DEFAULT 'camera',"
                             "  snapshot_zone_name TEXT,"
                             "  created_at TEXT NOT NULL DEFAULT "
                             "(datetime('now','localtime')),"
                             "  FOREIGN KEY (vehicle_id) REFERENCES "
                             "vehicles(vehicle_id) ON DELETE SET NULL,"
                             "  FOREIGN KEY (zone_id) REFERENCES "
                             "roi(zone_id) ON DELETE SET NULL"
                             ")"),
          errorMessage)) {
    return false;
  }

  const QStringList indexSqls = {
      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_entry "
                     "ON parking_logs(snapshot_camera_key, entry_time DESC)"),
      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_plate_active "
                     "ON parking_logs(snapshot_camera_key, snapshot_plate_number, exit_time)"),
      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_object_active "
                     "ON parking_logs(snapshot_camera_key, object_id, exit_time)"),
      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_reid_active "
                     "ON parking_logs(snapshot_camera_key, snapshot_reid_id, exit_time)"),
      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parking_logs_vehicle_id "
                     "ON parking_logs(vehicle_id)"),
      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parking_logs_zone_id "
                     "ON parking_logs(zone_id)")};

  for (const QString &sql : indexSqls) {
    if (!execSql(db, sql, errorMessage)) {
      return false;
    }
  }

  return true;
}

int ParkingRepository::insertEntry(const QString &cameraKey, int objectId,
                                   const QString &plateNumber,
                                   const QString &zoneId,
                                   const QString &zoneName,
                                   const QString &reidId,
                                   const QDateTime &entryTime,
                                   QString *errorMessage) {
  if (!init(errorMessage)) {
    return -1;
  }

  QSqlDatabase db = DatabaseContext::database();
  const QString normalizedPlate = normalizedPlateNumber(plateNumber);
  const QString normalizedReid = normalizedReidId(reidId);
  const int vehicleId = ensureVehicleId(normalizedPlate, normalizedReid,
                                        errorMessage);

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "INSERT INTO parking_logs "
      "(vehicle_id, zone_id, object_id, entry_time, pay_status, total_amount, "
      "snapshot_plate_number, snapshot_reid_id, snapshot_camera_key, "
      "snapshot_zone_name, created_at) "
      "VALUES (:vehicle_id, :zone_id, :object_id, :entry_time, :pay_status, "
      ":total_amount, :snapshot_plate_number, :snapshot_reid_id, "
      ":snapshot_camera_key, :snapshot_zone_name, datetime('now','localtime'))"));
  query.bindValue(":vehicle_id", nullableIntVariant(vehicleId));
  query.bindValue(":zone_id",
                  zoneId.trimmed().isEmpty() ? QVariant() : QVariant(zoneId.trimmed()));
  query.bindValue(":object_id", objectId);
  query.bindValue(":entry_time", entryTime.toString(Qt::ISODate));
  query.bindValue(":pay_status", kPendingPayStatus);
  query.bindValue(":total_amount", 0);
  query.bindValue(":snapshot_plate_number", normalizedPlate);
  query.bindValue(":snapshot_reid_id", normalizedReid);
  query.bindValue(":snapshot_camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":snapshot_zone_name", zoneName.trimmed());

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return -1;
  }

  return query.lastInsertId().toInt();
}

bool ParkingRepository::updateExit(int recordId, const QDateTime &exitTime,
                                   int *resolvedTotalAmount,
                                   QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return false;
  }

  QSqlQuery selectQuery(db);
  selectQuery.prepare(
      QStringLiteral("SELECT entry_time FROM parking_logs WHERE id = :id"));
  selectQuery.bindValue(":id", recordId);
  if (!selectQuery.exec() || !selectQuery.next()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Update error: failed to read entry_time");
    }
    return false;
  }

  QDateTime entryTime =
      QDateTime::fromString(selectQuery.value(0).toString(), Qt::ISODateWithMs);
  if (!entryTime.isValid()) {
    entryTime =
        QDateTime::fromString(selectQuery.value(0).toString(), Qt::ISODate);
  }
  const int totalAmount =
      parking::calculateParkingFee(entryTime, exitTime).totalAmount;
  if (resolvedTotalAmount) {
    *resolvedTotalAmount = totalAmount;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs "
      "SET exit_time = :exit_time, total_amount = :total_amount, "
      "pay_status = :pay_status "
      "WHERE id = :id"));
  query.bindValue(":exit_time", exitTime.toString(Qt::ISODate));
  query.bindValue(":total_amount", totalAmount);
  query.bindValue(":pay_status", kPendingPayStatus);
  query.bindValue(":id", recordId);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool ParkingRepository::updatePayment(const QString &cameraKey,
                                      const QString &plateNumber,
                                      int totalAmount,
                                      const QString &payStatus,
                                      QString *errorMessage) {
  const QJsonObject active =
      findActiveByPlate(cameraKey, plateNumber, errorMessage);
  if (active.isEmpty()) {
    return false;
  }

  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET pay_status = :pay_status, "
      "total_amount = :total_amount WHERE id = :id"));
  query.bindValue(":pay_status", normalizedPayStatus(payStatus, true));
  query.bindValue(":total_amount", std::max(0, totalAmount));
  query.bindValue(":id", active["id"].toInt());

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

QJsonObject ParkingRepository::findActiveByPlateAnyCamera(
    const QString &plateNumber, QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return QJsonObject();
  }

  QSqlQuery query(db);
  const QString sql = parkingLogSelectSql() + QStringLiteral(
                                             "WHERE %1 = :plate "
                                             "AND pl.exit_time IS NULL "
                                             "ORDER BY pl.entry_time DESC LIMIT 1")
                                             .arg(resolvedPlateExpr());
  query.prepare(sql);
  query.bindValue(":plate", normalizedPlateNumber(plateNumber));

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  return parkingLogFromQuery(query);
}

QList<QJsonObject> ParkingRepository::findLogsByExactPlateAnyCamera(
    const QString &plateNumber, int limit, QString *errorMessage) const {
  QList<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return results;
  }

  QSqlQuery query(db);
  const QString sql = parkingLogSelectSql() + QStringLiteral(
                                             "WHERE %1 = :plate "
                                             "ORDER BY pl.entry_time DESC LIMIT :limit")
                                             .arg(resolvedPlateExpr());
  query.prepare(sql);
  query.bindValue(":plate", normalizedPlateNumber(plateNumber));
  query.bindValue(":limit", limit);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return results;
  }

  while (query.next()) {
    results.append(parkingLogFromQuery(query));
  }
  return results;
}

QJsonObject ParkingRepository::findLatestUnpaidExitedByPlateAnyCamera(
    const QString &plateNumber, QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return QJsonObject();
  }

  QSqlQuery query(db);
  const QString sql = parkingLogSelectSql() + QStringLiteral(
                                             "WHERE %1 = :plate "
                                             "AND pl.exit_time IS NOT NULL "
                                             "AND pl.pay_status = :pay_status "
                                             "ORDER BY pl.exit_time DESC, pl.entry_time DESC "
                                             "LIMIT 1")
                                             .arg(resolvedPlateExpr());
  query.prepare(sql);
  query.bindValue(":plate", normalizedPlateNumber(plateNumber));
  query.bindValue(":pay_status", kPendingPayStatus);

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  return parkingLogFromQuery(query);
}

QJsonObject ParkingRepository::findLogById(int recordId,
                                           QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return QJsonObject();
  }

  QSqlQuery query(db);
  query.prepare(parkingLogSelectSql() +
                QStringLiteral("WHERE pl.id = :id LIMIT 1"));
  query.bindValue(":id", recordId);

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  return parkingLogFromQuery(query);
}

bool ParkingRepository::markPaymentById(int recordId, int totalAmount,
                                        const QString &payStatus,
                                        QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return false;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET pay_status = :pay_status, "
      "total_amount = :total_amount WHERE id = :id "
      "AND exit_time IS NOT NULL AND pay_status = :pending_status"));
  query.bindValue(":pay_status", normalizedPayStatus(payStatus, true));
  query.bindValue(":total_amount", std::max(0, totalAmount));
  query.bindValue(":id", recordId);
  query.bindValue(":pending_status", kPendingPayStatus);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

QJsonObject ParkingRepository::findActiveByObjectId(const QString &cameraKey,
                                                    int objectId,
                                                    QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return QJsonObject();
  }

  QSqlQuery query(db);
  query.prepare(parkingLogSelectSql() +
                QStringLiteral("WHERE pl.snapshot_camera_key = :camera_key "
                               "AND pl.object_id = :object_id "
                               "AND pl.exit_time IS NULL "
                               "ORDER BY pl.entry_time DESC LIMIT 1"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":object_id", objectId);

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  return parkingLogFromQuery(query);
}

QJsonObject ParkingRepository::findActiveByReidId(const QString &cameraKey,
                                                  const QString &reidId,
                                                  QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return QJsonObject();
  }

  const QString normalizedReid = normalizedReidId(reidId);
  if (normalizedReid.isEmpty()) {
    return QJsonObject();
  }

  QSqlQuery query(db);
  query.prepare(parkingLogSelectSql() +
                QStringLiteral("WHERE pl.snapshot_camera_key = :camera_key "
                               "AND pl.snapshot_reid_id = :reid_id "
                               "AND pl.exit_time IS NULL "
                               "ORDER BY pl.entry_time DESC LIMIT 1"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":reid_id", normalizedReid);

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  return parkingLogFromQuery(query);
}

bool ParkingRepository::updateActivePlateByObjectId(const QString &cameraKey,
                                                    int objectId,
                                                    const QString &plateNumber,
                                                    QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  const int recordId =
      findActiveRecordIdByObjectId(db, cameraKey, objectId, errorMessage);
  if (recordId < 0) {
    return false;
  }

  const QString normalizedPlate = normalizedPlateNumber(plateNumber);
  const int vehicleId = ensureVehicleId(normalizedPlate, QString(), errorMessage);

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs "
      "SET snapshot_plate_number = :plate, "
      "    vehicle_id = COALESCE(:vehicle_id, vehicle_id) "
      "WHERE id = :id"));
  query.bindValue(":plate", normalizedPlate);
  query.bindValue(":vehicle_id", nullableIntVariant(vehicleId));
  query.bindValue(":id", recordId);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

bool ParkingRepository::updateActivePlateByReidId(const QString &cameraKey,
                                                  const QString &reidId,
                                                  const QString &plateNumber,
                                                  QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  const int recordId =
      findActiveRecordIdByReid(db, cameraKey, reidId, errorMessage);
  if (recordId < 0) {
    return false;
  }

  const QString normalizedPlate = normalizedPlateNumber(plateNumber);
  const QString normalizedReid = normalizedReidId(reidId);
  const int vehicleId =
      ensureVehicleId(normalizedPlate, normalizedReid, errorMessage);

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs "
      "SET snapshot_plate_number = :plate, "
      "    vehicle_id = COALESCE(:vehicle_id, vehicle_id) "
      "WHERE id = :id"));
  query.bindValue(":plate", normalizedPlate);
  query.bindValue(":vehicle_id", nullableIntVariant(vehicleId));
  query.bindValue(":id", recordId);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

bool ParkingRepository::updateActiveObjectIdByReidId(const QString &cameraKey,
                                                     const QString &reidId,
                                                     int objectId,
                                                     QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  const int recordId =
      findActiveRecordIdByReid(db, cameraKey, reidId, errorMessage);
  if (recordId < 0) {
    return false;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET object_id = :object_id WHERE id = :id"));
  query.bindValue(":object_id", objectId);
  query.bindValue(":id", recordId);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

bool ParkingRepository::updateActiveReidByObjectId(const QString &cameraKey,
                                                   int objectId,
                                                   const QString &reidId,
                                                   QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  const int recordId =
      findActiveRecordIdByObjectId(db, cameraKey, objectId, errorMessage);
  if (recordId < 0) {
    return false;
  }

  const QString normalizedReid = normalizedReidId(reidId);
  if (normalizedReid.isEmpty()) {
    return false;
  }

  const int vehicleId = ensureVehicleId(QString(), normalizedReid, errorMessage);

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs "
      "SET snapshot_reid_id = :reid_id, "
      "    vehicle_id = COALESCE(:vehicle_id, vehicle_id) "
      "WHERE id = :id"));
  query.bindValue(":reid_id", normalizedReid);
  query.bindValue(":vehicle_id", nullableIntVariant(vehicleId));
  query.bindValue(":id", recordId);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

QJsonObject ParkingRepository::findActiveByPlate(const QString &cameraKey,
                                                 const QString &plateNumber,
                                                 QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return QJsonObject();
  }

  QSqlQuery query(db);
  const QString sql = parkingLogSelectSql() + QStringLiteral(
                                             "WHERE pl.snapshot_camera_key = :camera_key "
                                             "AND %1 = :plate "
                                             "AND pl.exit_time IS NULL "
                                             "ORDER BY pl.entry_time DESC LIMIT 1")
                                             .arg(resolvedPlateExpr());
  query.prepare(sql);
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":plate", normalizedPlateNumber(plateNumber));

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  return parkingLogFromQuery(query);
}

QList<QJsonObject> ParkingRepository::recentLogs(const QString &cameraKey,
                                                 int limit,
                                                 QString *errorMessage) const {
  QList<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return results;
  }

  QSqlQuery query(db);
  query.prepare(parkingLogSelectSql() +
                QStringLiteral("WHERE pl.snapshot_camera_key = :camera_key "
                               "ORDER BY pl.entry_time DESC LIMIT :limit"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":limit", limit);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return results;
  }

  while (query.next()) {
    results.append(parkingLogFromQuery(query));
  }
  return results;
}

QList<QJsonObject>
ParkingRepository::searchByPlate(const QString &cameraKey, const QString &plate,
                                 QString *errorMessage) const {
  QList<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return results;
  }

  QSqlQuery query(db);
  const QString sql = parkingLogSelectSql() + QStringLiteral(
                                             "WHERE pl.snapshot_camera_key = :camera_key "
                                             "AND %1 LIKE :plate "
                                             "ORDER BY pl.entry_time DESC LIMIT 100")
                                             .arg(resolvedPlateExpr());
  query.prepare(sql);
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":plate",
                  QStringLiteral("%") + normalizedPlateNumber(plate) +
                      QStringLiteral("%"));

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return results;
  }

  while (query.next()) {
    results.append(parkingLogFromQuery(query));
  }
  return results;
}

bool ParkingRepository::updatePlate(const QString &cameraKey, int recordId,
                                    const QString &newPlate,
                                    QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return false;
  }

  const QString normalizedPlate = normalizedPlateNumber(newPlate);
  const int vehicleId = ensureVehicleId(normalizedPlate, QString(), errorMessage);

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs "
      "SET snapshot_plate_number = :plate, "
      "    vehicle_id = COALESCE(:vehicle_id, vehicle_id) "
      "WHERE id = :id AND snapshot_camera_key = :camera_key"));
  query.bindValue(":plate", normalizedPlate);
  query.bindValue(":vehicle_id", nullableIntVariant(vehicleId));
  query.bindValue(":id", recordId);
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

QList<QJsonObject> ParkingRepository::getAllLogs(const QString &cameraKey,
                                                 QString *errorMessage) const {
  return recentLogs(cameraKey, 100000, errorMessage);
}

bool ParkingRepository::deleteLog(const QString &cameraKey, int id,
                                  QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Database is not open");
    }
    return false;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "DELETE FROM parking_logs WHERE id = :id "
      "AND snapshot_camera_key = :camera_key"));
  query.bindValue(":id", id);
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  if (query.numRowsAffected() <= 0) {
    if (errorMessage) {
      *errorMessage =
          QStringLiteral("No parking log found for the selected camera scope.");
    }
    return false;
  }
  return true;
}

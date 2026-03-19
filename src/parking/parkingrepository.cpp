#include "parking/parkingrepository.h"
#include "parking/parkingfeepolicy.h"
#include "database/databasecontext.h"
#include <QDebug>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>

namespace {
const QString kDefaultCameraKey = QStringLiteral("camera");
const QString kPendingPayStatus = QStringLiteral("정산대기");
const QString kPaidPayStatus = QStringLiteral("결제완료");

bool isAllCameraKey(const QString &cameraKey) {
  const QString trimmed = cameraKey.trimmed();
  return trimmed == QStringLiteral("*") ||
         trimmed.compare(QStringLiteral("__all__"), Qt::CaseInsensitive) == 0;
}

QString normalizedCameraKey(const QString &cameraKey) {
  const QString trimmed = cameraKey.trimmed();
  return trimmed.isEmpty() ? kDefaultCameraKey : trimmed;
}

QString normalizedPlateNumber(const QString &plateNumber) {
  QString normalized = plateNumber;
  if (normalized.isNull()) {
    normalized = QStringLiteral("");
  }
  return normalized.trimmed();
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

bool ensureColumn(QSqlDatabase &db, const QString &tableName,
                  const QString &columnName, const QString &definition,
                  QString *errorMessage) {
  QSqlQuery pragma(db);
  if (!pragma.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
    if (errorMessage) {
      *errorMessage = pragma.lastError().text();
    }
    return false;
  }

  while (pragma.next()) {
    if (pragma.value(1).toString() == columnName) {
      return true;
    }
  }

  QSqlQuery alter(db);
  if (!alter.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                      .arg(tableName, columnName, definition))) {
    if (errorMessage) {
      *errorMessage = alter.lastError().text();
    }
    return false;
  }
  return true;
}
} // namespace

ParkingRepository::ParkingRepository() {}

// 통합 DB 사용 시 init은 스키마 확인만 수행
bool ParkingRepository::init(QString *errorMessage) {
  return ensureSchema(errorMessage);
}

bool ParkingRepository::ensureSchema(QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = QStringLiteral("Database is not open");
    return false;
  }

  QSqlQuery query(db);

  const QString sql =
      QStringLiteral("CREATE TABLE IF NOT EXISTS parking_logs ("
                     "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "  camera_key TEXT NOT NULL DEFAULT 'camera',"
                     "  object_id INTEGER NOT NULL DEFAULT -1,"
                     "  plate_number TEXT NOT NULL DEFAULT '',"
                     "  zone_name TEXT DEFAULT '',"
                     "  roi_index INTEGER NOT NULL,"
                     "  reid_id TEXT NOT NULL DEFAULT '',"
                     "  entry_time TEXT NOT NULL,"
                     "  exit_time TEXT,"
                     "  pay_status TEXT DEFAULT '정산대기',"
                     "  total_amount INTEGER DEFAULT 0,"
                     "  bestshot_path TEXT,"
                     "  ocr_confidence REAL DEFAULT 0.0,"
                     "  created_at TEXT DEFAULT (datetime('now','localtime'))"
                     ")");

  if (!query.exec(sql)) {
    const QString err =
        QStringLiteral("Schema error: ") + query.lastError().text();
    qWarning() << err;
    if (errorMessage) {
      *errorMessage = err;
    }
    return false;
  }

  if (!ensureColumn(db, QStringLiteral("parking_logs"), QStringLiteral("reid_id"),
                    QStringLiteral("TEXT NOT NULL DEFAULT ''"), errorMessage)) {
    return false;
  }

  QSqlQuery indexQuery(db);
  indexQuery.exec(QStringLiteral(
      "CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_entry "
      "ON parking_logs(camera_key, entry_time DESC)"));
  indexQuery.exec(QStringLiteral(
      "CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_plate_active "
      "ON parking_logs(camera_key, plate_number, exit_time)"));
  indexQuery.exec(QStringLiteral(
      "CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_object_active "
      "ON parking_logs(camera_key, object_id, exit_time)"));

  return true;
}

int ParkingRepository::insertEntry(const QString &cameraKey, int objectId,
                                   const QString &plateNumber,
                                   const QString &zoneName, int roiIndex,
                                   const QString &reidId,
                                   const QDateTime &entryTime,
                                   QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return -1;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "INSERT INTO parking_logs (camera_key, object_id, plate_number, "
      "zone_name, roi_index, reid_id, entry_time, pay_status, total_amount, "
      "ocr_confidence, bestshot_path) "
      "VALUES (:camera_key, :object_id, :plate, :zone_name, :roi, :reid_id, "
      ":entry, :pay_status, :total_amount, :ocr_confidence, "
      ":bestshot_path)"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":object_id", objectId);
  query.bindValue(":plate", normalizedPlateNumber(plateNumber));
  query.bindValue(":zone_name", zoneName.trimmed());
  query.bindValue(":roi", roiIndex);
  query.bindValue(":reid_id", reidId.trimmed());
  query.bindValue(":entry", entryTime.toString(Qt::ISODate));
  query.bindValue(":pay_status", kPendingPayStatus);
  query.bindValue(":total_amount", 0);
  query.bindValue(":ocr_confidence", 0.0);
  query.bindValue(":bestshot_path", QStringLiteral(""));

  if (!query.exec()) {
    const QString err =
        QStringLiteral("Insert error: ") + query.lastError().text();
    qWarning() << err;
    if (errorMessage) {
      *errorMessage = err;
    }
    return -1;
  }

  return query.lastInsertId().toInt();
}

bool ParkingRepository::updateExit(int recordId, const QDateTime &exitTime,
                                   int *totalAmount,
                                   QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  QSqlQuery selectQuery(db);
  selectQuery.prepare(
      QStringLiteral("SELECT entry_time FROM parking_logs WHERE id = :id"));
  selectQuery.bindValue(":id", recordId);
  if (!selectQuery.exec() || !selectQuery.next()) {
    const QString err =
        QStringLiteral("Update error: failed to read entry_time");
    if (errorMessage) {
      *errorMessage = err;
    }
    return false;
  }

  QDateTime entryTime =
      QDateTime::fromString(selectQuery.value(0).toString(), Qt::ISODateWithMs);
  if (!entryTime.isValid()) {
    entryTime = QDateTime::fromString(selectQuery.value(0).toString(),
                                      Qt::ISODate);
  }
  const parking::ParkingFeeResult feeResult =
      parking::calculateParkingFee(entryTime, exitTime);
  const int calculatedTotalAmount = feeResult.totalAmount;
  if (totalAmount) {
    *totalAmount = calculatedTotalAmount;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET exit_time = :exit, total_amount = :total_amount, "
      "pay_status = :pay_status WHERE id = :id"));
  query.bindValue(":exit", exitTime.toString(Qt::ISODate));
  query.bindValue(":total_amount", calculatedTotalAmount);
  query.bindValue(":pay_status",
                  calculatedTotalAmount <= 0 ? kPaidPayStatus
                                             : kPendingPayStatus);
  query.bindValue(":id", recordId);

  if (!query.exec()) {
    const QString err =
        QStringLiteral("Update error: ") + query.lastError().text();
    qWarning() << err;
    if (errorMessage) {
      *errorMessage = err;
    }
    return false;
  }
  return true;
}

bool ParkingRepository::updatePayment(const QString &cameraKey,
                                      const QString &plateNumber,
                                      int totalAmount,
                                      const QString &payStatus,
                                      QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  const QString normalizedKey = normalizedCameraKey(cameraKey);
  const QString normalizedStatus = normalizedPayStatus(payStatus, true);
  const int normalizedAmount = std::max(0, totalAmount);

  auto updateTarget = [&](const QString &idQuerySql) -> int {
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE parking_logs SET pay_status = :pay_status, "
        "total_amount = :total_amount "
        "WHERE id = (") +
                  idQuerySql + QStringLiteral(")"));
    query.bindValue(":pay_status", normalizedStatus);
    query.bindValue(":total_amount", normalizedAmount);
    query.bindValue(":camera_key", normalizedKey);
    query.bindValue(":plate", plateNumber);

    if (!query.exec()) {
      const QString err =
          QStringLiteral("Update payment error: ") + query.lastError().text();
      qWarning() << err;
      if (errorMessage) {
        *errorMessage = err;
      }
      return -1;
    }

    return query.numRowsAffected();
  };

  const int pendingExitUpdated = updateTarget(QStringLiteral(
      "SELECT id FROM parking_logs WHERE camera_key = :camera_key "
      "AND plate_number = :plate AND exit_time IS NOT NULL "
      "AND pay_status != '결제완료' "
      "ORDER BY exit_time DESC LIMIT 1"));
  if (pendingExitUpdated < 0) {
    return false;
  }
  if (pendingExitUpdated > 0) {
    return true;
  }

  const int activeUpdated = updateTarget(QStringLiteral(
      "SELECT id FROM parking_logs WHERE camera_key = :camera_key "
      "AND plate_number = :plate AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  return activeUpdated > 0;
}

QJsonObject ParkingRepository::findActiveByObjectId(const QString &cameraKey,
                                                    int objectId,
                                                    QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
      "reid_id, entry_time, pay_status, total_amount "
      "FROM parking_logs WHERE camera_key = :camera_key "
      "AND object_id = :object_id AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":object_id", objectId);

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  QJsonObject record;
  record["id"] = query.value("id").toInt();
  record["camera_key"] = query.value("camera_key").toString();
  record["object_id"] = query.value("object_id").toInt();
  record["plate_number"] = query.value("plate_number").toString();
  record["zone_name"] = query.value("zone_name").toString();
  record["roi_index"] = query.value("roi_index").toInt();
  record["reid_id"] = query.value("reid_id").toString();
  record["entry_time"] = query.value("entry_time").toString();
  record["pay_status"] = query.value("pay_status").toString();
  record["total_amount"] = query.value("total_amount").toInt();
  return record;
}

bool ParkingRepository::updateActivePlateByObjectId(const QString &cameraKey,
                                                    int objectId,
                                                    const QString &plateNumber,
                                                    QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET plate_number = :plate "
      "WHERE id = ("
      "  SELECT id FROM parking_logs WHERE camera_key = :camera_key "
      "  AND object_id = :object_id AND exit_time IS NULL "
      "  ORDER BY entry_time DESC LIMIT 1"
      ")"));
  query.bindValue(":plate", normalizedPlateNumber(plateNumber));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":object_id", objectId);

  if (!query.exec()) {
    const QString err =
        QStringLiteral("Update active plate error: ") + query.lastError().text();
    qWarning() << err;
    if (errorMessage) {
      *errorMessage = err;
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
  if (!db.isOpen()) {
    return false;
  }

  const QString trimmedReid = reidId.trimmed();
  if (trimmedReid.isEmpty() || trimmedReid == QStringLiteral("V---")) {
    return false;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET reid_id = :reid_id "
      "WHERE id = ("
      "  SELECT id FROM parking_logs WHERE camera_key = :camera_key "
      "  AND object_id = :object_id AND exit_time IS NULL "
      "  ORDER BY entry_time DESC LIMIT 1"
      ")"));
  query.bindValue(":reid_id", trimmedReid);
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":object_id", objectId);

  if (!query.exec()) {
    const QString err =
        QStringLiteral("Update active reid error: ") + query.lastError().text();
    qWarning() << err;
    if (errorMessage) {
      *errorMessage = err;
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
  if (!db.isOpen()) {
    return false;
  }

  const QString trimmedReid = reidId.trimmed();
  if (trimmedReid.isEmpty() || trimmedReid == QStringLiteral("V---")) {
    return false;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET plate_number = :plate "
      "WHERE id = ("
      "  SELECT id FROM parking_logs WHERE camera_key = :camera_key "
      "  AND reid_id = :reid_id AND exit_time IS NULL "
      "  ORDER BY entry_time DESC LIMIT 1"
      ")"));
  query.bindValue(":plate", normalizedPlateNumber(plateNumber));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":reid_id", trimmedReid);

  if (!query.exec()) {
    const QString err = QStringLiteral("Update active plate by reid error: ") +
                        query.lastError().text();
    qWarning() << err;
    if (errorMessage) {
      *errorMessage = err;
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

QJsonObject ParkingRepository::findActiveByPlate(const QString &cameraKey,
                                                 const QString &plateNumber,
                                                 QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  if (isAllCameraKey(cameraKey)) {
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, pay_status, total_amount "
        "FROM parking_logs WHERE plate_number = :plate AND exit_time IS NULL "
        "ORDER BY entry_time DESC LIMIT 1"));
  } else {
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, pay_status, total_amount "
        "FROM parking_logs WHERE camera_key = :camera_key "
        "AND plate_number = :plate AND exit_time IS NULL "
        "ORDER BY entry_time DESC LIMIT 1"));
    query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  }
  query.bindValue(":plate", plateNumber);

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  QJsonObject record;
  record["id"] = query.value("id").toInt();
  record["camera_key"] = query.value("camera_key").toString();
  record["object_id"] = query.value("object_id").toInt();
  record["plate_number"] = query.value("plate_number").toString();
  record["zone_name"] = query.value("zone_name").toString();
  record["roi_index"] = query.value("roi_index").toInt();
  record["reid_id"] = query.value("reid_id").toString();
  record["entry_time"] = query.value("entry_time").toString();
  record["pay_status"] = query.value("pay_status").toString();
  record["total_amount"] = query.value("total_amount").toInt();
  return record;
}

QJsonObject ParkingRepository::findLatestPendingPaymentByPlate(
    const QString &cameraKey, const QString &plateNumber,
    QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  if (isAllCameraKey(cameraKey)) {
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, exit_time, pay_status, total_amount "
        "FROM parking_logs WHERE plate_number = :plate "
        "AND exit_time IS NOT NULL AND pay_status != '결제완료' "
        "ORDER BY exit_time DESC LIMIT 1"));
  } else {
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, exit_time, pay_status, total_amount "
        "FROM parking_logs WHERE camera_key = :camera_key "
        "AND plate_number = :plate AND exit_time IS NOT NULL "
        "AND pay_status != '결제완료' "
        "ORDER BY exit_time DESC LIMIT 1"));
    query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  }
  query.bindValue(":plate", plateNumber);

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  QJsonObject record;
  record["id"] = query.value("id").toInt();
  record["camera_key"] = query.value("camera_key").toString();
  record["object_id"] = query.value("object_id").toInt();
  record["plate_number"] = query.value("plate_number").toString();
  record["zone_name"] = query.value("zone_name").toString();
  record["roi_index"] = query.value("roi_index").toInt();
  record["reid_id"] = query.value("reid_id").toString();
  record["entry_time"] = query.value("entry_time").toString();
  record["exit_time"] = query.value("exit_time").toString();
  record["pay_status"] = query.value("pay_status").toString();
  record["total_amount"] = query.value("total_amount").toInt();
  return record;
}

QList<QJsonObject> ParkingRepository::recentLogs(const QString &cameraKey,
                                                 int limit,
                                                 QString *errorMessage) const {
  QList<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return results;

  QSqlQuery query(db);
  if (isAllCameraKey(cameraKey)) {
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, exit_time, pay_status, total_amount "
        "FROM parking_logs "
        "ORDER BY entry_time DESC LIMIT :limit"));
  } else {
    const QString normalizedKey = normalizedCameraKey(cameraKey);
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, exit_time, pay_status, total_amount "
        "FROM parking_logs WHERE camera_key = :camera_key "
        "ORDER BY entry_time DESC LIMIT :limit"));
    query.bindValue(":camera_key", normalizedKey);
  }
  query.bindValue(":limit", limit);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["id"] = query.value("id").toInt();
    row["camera_key"] = query.value("camera_key").toString();
    row["object_id"] = query.value("object_id").toInt();
    row["plate_number"] = query.value("plate_number").toString();
    row["zone_name"] = query.value("zone_name").toString();
    row["roi_index"] = query.value("roi_index").toInt();
    row["reid_id"] = query.value("reid_id").toString();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
    row["pay_status"] = query.value("pay_status").toString();
    row["total_amount"] = query.value("total_amount").toInt();
    results.append(row);
  }
  return results;
}

QList<QJsonObject>
ParkingRepository::searchByPlate(const QString &cameraKey, const QString &plate,
                                 QString *errorMessage) const {
  QList<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return results;

  QSqlQuery query(db);
  if (isAllCameraKey(cameraKey)) {
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, exit_time, pay_status, total_amount "
        "FROM parking_logs WHERE plate_number LIKE :plate "
        "ORDER BY entry_time DESC LIMIT 100"));
  } else {
    const QString normalizedKey = normalizedCameraKey(cameraKey);
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, exit_time, pay_status, total_amount "
        "FROM parking_logs WHERE camera_key = :camera_key "
        "AND plate_number LIKE :plate "
        "ORDER BY entry_time DESC LIMIT 100"));
    query.bindValue(":camera_key", normalizedKey);
  }
  query.bindValue(":plate", QStringLiteral("%") + plate + QStringLiteral("%"));

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["id"] = query.value("id").toInt();
    row["camera_key"] = query.value("camera_key").toString();
    row["object_id"] = query.value("object_id").toInt();
    row["plate_number"] = query.value("plate_number").toString();
    row["zone_name"] = query.value("zone_name").toString();
    row["roi_index"] = query.value("roi_index").toInt();
    row["reid_id"] = query.value("reid_id").toString();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
    row["pay_status"] = query.value("pay_status").toString();
    row["total_amount"] = query.value("total_amount").toInt();
    results.append(row);
  }
  return results;
}

QList<QJsonObject> ParkingRepository::recentLogsByPlate(
    const QString &cameraKey, const QString &plateNumber, int limit,
    QString *errorMessage) const {
  QList<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return results;
  }

  const QString normalizedPlate = normalizedPlateNumber(plateNumber);
  if (normalizedPlate.isEmpty()) {
    return results;
  }

  QSqlQuery query(db);
  if (isAllCameraKey(cameraKey)) {
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, exit_time, pay_status, total_amount "
        "FROM parking_logs WHERE plate_number = :plate "
        "ORDER BY entry_time DESC LIMIT :limit"));
  } else {
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, exit_time, pay_status, total_amount "
        "FROM parking_logs WHERE camera_key = :camera_key "
        "AND plate_number = :plate "
        "ORDER BY entry_time DESC LIMIT :limit"));
    query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  }
  query.bindValue(":plate", normalizedPlate);
  query.bindValue(":limit", std::max(1, limit));

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["id"] = query.value("id").toInt();
    row["camera_key"] = query.value("camera_key").toString();
    row["object_id"] = query.value("object_id").toInt();
    row["plate_number"] = query.value("plate_number").toString();
    row["zone_name"] = query.value("zone_name").toString();
    row["roi_index"] = query.value("roi_index").toInt();
    row["reid_id"] = query.value("reid_id").toString();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
    row["pay_status"] = query.value("pay_status").toString();
    row["total_amount"] = query.value("total_amount").toInt();
    results.append(row);
  }
  return results;
}

bool ParkingRepository::updatePlate(const QString &cameraKey, int recordId,
                                    const QString &newPlate,
                                    QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET plate_number = :plate "
      "WHERE id = :id AND camera_key = :camera_key"));
  query.bindValue(":plate", normalizedPlateNumber(newPlate));
  query.bindValue(":id", recordId);
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));

  if (!query.exec()) {
    const QString err =
        QStringLiteral("Update plate error: ") + query.lastError().text();
    qWarning() << err;
    if (errorMessage) {
      *errorMessage = err;
    }
    return false;
  }
  return true;
}

QList<QJsonObject>
ParkingRepository::getAllLogs(const QString &cameraKey,
                              QString *errorMessage) const {
  QList<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();

  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = "Database is not open";
    return results;
  }

  QSqlQuery query(db);
  if (isAllCameraKey(cameraKey)) {
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, exit_time, pay_status, total_amount "
        "FROM parking_logs "
        "ORDER BY entry_time DESC"));
  } else {
    const QString normalizedKey = normalizedCameraKey(cameraKey);
    query.prepare(QStringLiteral(
        "SELECT id, camera_key, object_id, plate_number, zone_name, roi_index, "
        "reid_id, entry_time, exit_time, pay_status, total_amount "
        "FROM parking_logs WHERE camera_key = :camera_key "
        "ORDER BY entry_time DESC"));
    query.bindValue(":camera_key", normalizedKey);
  }

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["id"] = query.value("id").toInt();
    row["camera_key"] = query.value("camera_key").toString();
    row["object_id"] = query.value("object_id").toInt();
    row["plate_number"] = query.value("plate_number").toString();
    row["zone_name"] = query.value("zone_name").toString();
    row["roi_index"] = query.value("roi_index").toInt();
    row["reid_id"] = query.value("reid_id").toString();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
    row["pay_status"] = query.value("pay_status").toString();
    row["total_amount"] = query.value("total_amount").toInt();
    results.append(row);
  }
  return results;
}

bool ParkingRepository::deleteLog(const QString &cameraKey, int id,
                                  QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();

  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = "Database is not open";
    return false;
  }

  QSqlQuery query(db);
  query.prepare("DELETE FROM parking_logs WHERE id = :id AND camera_key = "
                ":camera_key");
  query.bindValue(":id", id);
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
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

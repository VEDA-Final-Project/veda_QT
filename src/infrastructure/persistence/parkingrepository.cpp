#include "infrastructure/persistence/parkingrepository.h"
#include "domain/parking/parkingfeepolicy.h"
#include "infrastructure/persistence/databasecontext.h"
#include "infrastructure/security/dataprotection.h"
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

QString encryptPlateNumber(const QString &plateNumber) {
  return DataProtection::instance().encryptString(normalizedPlateNumber(plateNumber));
}

QString plateLookupToken(const QString &plateNumber) {
  return DataProtection::instance().lookupToken(QStringLiteral("parking_plate"),
                                                normalizedPlateNumber(plateNumber));
}

QString resolveStoredPlate(const QSqlQuery &query) {
  const QString encryptedPlate = query.value("plate_number_enc").toString();
  if (!encryptedPlate.isEmpty()) {
    return DataProtection::instance().decryptString(encryptedPlate);
  }
  return query.value("plate_number").toString().trimmed();
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

bool migrateParkingPlateStorage(QSqlDatabase db, QString *errorMessage) {
  QSqlQuery query(db);
  if (!query.exec(QStringLiteral(
          "SELECT id, plate_number, plate_number_enc FROM parking_logs"))) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  while (query.next()) {
    const int id = query.value("id").toInt();
    const QString currentStored = query.value("plate_number").toString();
    const QString resolvedPlate = resolveStoredPlate(query);
    const QString lookupToken = plateLookupToken(resolvedPlate);
    const QString encryptedPlate = encryptPlateNumber(resolvedPlate);
    const QString currentEncrypted = query.value("plate_number_enc").toString();

    if (currentStored == lookupToken && currentEncrypted == encryptedPlate) {
      continue;
    }

    QSqlQuery updateQuery(db);
    updateQuery.prepare(QStringLiteral(
        "UPDATE parking_logs SET plate_number = :plate_hash, "
        "plate_number_enc = :plate_enc WHERE id = :id"));
    updateQuery.bindValue(":plate_hash", lookupToken);
    updateQuery.bindValue(":plate_enc", encryptedPlate);
    updateQuery.bindValue(":id", id);

    if (!updateQuery.exec()) {
      if (errorMessage) {
        *errorMessage = updateQuery.lastError().text();
      }
      return false;
    }
  }

  return true;
}

QString normalizedReidId(const QString &reidId) {
  return reidId.trimmed();
}

bool hasUsableReidId(const QString &reidId) {
  const QString normalized = normalizedReidId(reidId);
  return !normalized.isEmpty() && normalized != QStringLiteral("V---");
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
                     "  plate_number_enc TEXT NOT NULL DEFAULT '',"
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

  QSqlQuery alterQuery(db);
  alterQuery.exec(QStringLiteral(
      "ALTER TABLE parking_logs ADD COLUMN plate_number_enc TEXT NOT NULL DEFAULT ''"));

  QSqlQuery indexQuery(db);
  indexQuery.exec(
      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_entry "
                     "ON parking_logs(camera_key, entry_time DESC)"));
  indexQuery.exec(QStringLiteral(
      "CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_plate_active "
      "ON parking_logs(camera_key, plate_number, exit_time)"));
  indexQuery.exec(QStringLiteral(
      "CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_object_active "
      "ON parking_logs(camera_key, object_id, exit_time)"));

  if (!migrateParkingPlateStorage(db, errorMessage)) {
    return false;
  }

  indexQuery.exec(QStringLiteral(
      "CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_reid_active "
      "ON parking_logs(camera_key, reid_id, exit_time)"));

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
      "INSERT INTO parking_logs (camera_key, object_id, plate_number, plate_number_enc, "
      "zone_name, roi_index, reid_id, entry_time, pay_status, total_amount, "
      "ocr_confidence, bestshot_path) "
      "VALUES (:camera_key, :object_id, :plate_hash, :plate_enc, :zone_name, :roi, :reid_id, "
      ":entry, "
      ":pay_status, :total_amount, :ocr_confidence, "
      ":bestshot_path)"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":object_id", objectId);
  query.bindValue(":plate_hash", plateLookupToken(plateNumber));
  query.bindValue(":plate_enc", encryptPlateNumber(plateNumber));
  query.bindValue(":zone_name", zoneName.trimmed());
  query.bindValue(":roi", roiIndex);
  query.bindValue(":reid_id", reidId);
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
                                   int *resolvedTotalAmount,
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
    entryTime =
        QDateTime::fromString(selectQuery.value(0).toString(), Qt::ISODate);
  }
  const int totalAmount =
      parking::calculateParkingFee(entryTime, exitTime).totalAmount;
  if (resolvedTotalAmount) {
    *resolvedTotalAmount = totalAmount;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral("UPDATE parking_logs SET exit_time = :exit, "
                               "total_amount = :total_amount, "
                               "pay_status = :pay_status WHERE id = :id"));
  query.bindValue(":exit", exitTime.toString(Qt::ISODate));
  query.bindValue(":total_amount", totalAmount);
  query.bindValue(":pay_status", kPendingPayStatus);
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
                                      int totalAmount, const QString &payStatus,
                                      QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  // 1단계: 대상 레코드 ID 조회 (SELECT)
  QSqlQuery selectQuery(db);
  selectQuery.prepare(QStringLiteral(
      "SELECT id FROM parking_logs WHERE camera_key = :camera_key "
      "AND plate_number = :plate AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  selectQuery.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  selectQuery.bindValue(":plate", plateLookupToken(plateNumber));

  if (!selectQuery.exec() || !selectQuery.next()) {
    return false;
  }
  int recordId = selectQuery.value(0).toInt();

  // 2단계: ID 기반 직접 업데이트 (UPDATE)
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET pay_status = :pay_status, "
      "total_amount = :total_amount WHERE id = :id"));
  query.bindValue(":pay_status", normalizedPayStatus(payStatus, true));
  query.bindValue(":total_amount", std::max(0, totalAmount));
  query.bindValue(":id", recordId);

  if (!query.exec()) {
    const QString err =
        QStringLiteral("Update payment error: ") + query.lastError().text();
    qWarning() << err;
    if (errorMessage) {
      *errorMessage = err;
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

QJsonObject ParkingRepository::findActiveByPlateAnyCamera(
    const QString &plateNumber, QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, object_id, plate_number, plate_number_enc, reid_id, zone_name, "
      "roi_index, entry_time, exit_time, pay_status, total_amount "
      "FROM parking_logs WHERE plate_number = :plate AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  query.bindValue(":plate", plateLookupToken(plateNumber));

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
  record["plate_number"] = resolveStoredPlate(query);
  record["reid_id"] = query.value("reid_id").toString();
  record["zone_name"] = query.value("zone_name").toString();
  record["roi_index"] = query.value("roi_index").toInt();
  record["entry_time"] = query.value("entry_time").toString();
  record["exit_time"] = query.value("exit_time").toString();
  record["pay_status"] = query.value("pay_status").toString();
  record["total_amount"] = query.value("total_amount").toInt();
  return record;
}

QList<QJsonObject> ParkingRepository::findLogsByExactPlateAnyCamera(
    const QString &plateNumber, int limit, QString *errorMessage) const {
  QList<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return results;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, object_id, plate_number, plate_number_enc, reid_id, zone_name, "
      "roi_index, entry_time, exit_time, pay_status, total_amount "
      "FROM parking_logs WHERE plate_number = :plate "
      "ORDER BY entry_time DESC LIMIT :limit"));
  query.bindValue(":plate", plateLookupToken(plateNumber));
  query.bindValue(":limit", limit);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return results;
  }

  while (query.next()) {
    QJsonObject record;
    record["id"] = query.value("id").toInt();
    record["camera_key"] = query.value("camera_key").toString();
    record["object_id"] = query.value("object_id").toInt();
    record["plate_number"] = resolveStoredPlate(query);
    record["reid_id"] = query.value("reid_id").toString();
    record["zone_name"] = query.value("zone_name").toString();
    record["roi_index"] = query.value("roi_index").toInt();
    record["entry_time"] = query.value("entry_time").toString();
    record["exit_time"] = query.value("exit_time").toString();
    record["pay_status"] = query.value("pay_status").toString();
    record["total_amount"] = query.value("total_amount").toInt();
    results.append(record);
  }
  return results;
}

QJsonObject ParkingRepository::findLatestUnpaidExitedByPlateAnyCamera(
    const QString &plateNumber, QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, object_id, plate_number, plate_number_enc, reid_id, zone_name, "
      "roi_index, entry_time, exit_time, pay_status, total_amount "
      "FROM parking_logs WHERE plate_number = :plate "
      "AND exit_time IS NOT NULL AND pay_status = :pay_status "
      "ORDER BY exit_time DESC, entry_time DESC LIMIT 1"));
  query.bindValue(":plate", plateLookupToken(plateNumber));
  query.bindValue(":pay_status", kPendingPayStatus);

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
  record["plate_number"] = resolveStoredPlate(query);
  record["reid_id"] = query.value("reid_id").toString();
  record["zone_name"] = query.value("zone_name").toString();
  record["roi_index"] = query.value("roi_index").toInt();
  record["entry_time"] = query.value("entry_time").toString();
  record["exit_time"] = query.value("exit_time").toString();
  record["pay_status"] = query.value("pay_status").toString();
  record["total_amount"] = query.value("total_amount").toInt();
  return record;
}

QJsonObject ParkingRepository::findLogById(int recordId,
                                           QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, object_id, plate_number, plate_number_enc, reid_id, zone_name, "
      "roi_index, entry_time, exit_time, pay_status, total_amount "
      "FROM parking_logs WHERE id = :id LIMIT 1"));
  query.bindValue(":id", recordId);

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
  record["plate_number"] = resolveStoredPlate(query);
  record["reid_id"] = query.value("reid_id").toString();
  record["zone_name"] = query.value("zone_name").toString();
  record["roi_index"] = query.value("roi_index").toInt();
  record["entry_time"] = query.value("entry_time").toString();
  record["exit_time"] = query.value("exit_time").toString();
  record["pay_status"] = query.value("pay_status").toString();
  record["total_amount"] = query.value("total_amount").toInt();
  return record;
}

bool ParkingRepository::markPaymentById(int recordId, int totalAmount,
                                        const QString &payStatus,
                                        QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

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
    const QString err =
        QStringLiteral("Update payment by id error: ") +
        query.lastError().text();
    qWarning() << err;
    if (errorMessage) {
      *errorMessage = err;
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

QJsonObject
ParkingRepository::findActiveByObjectId(const QString &cameraKey, int objectId,
                                        QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, object_id, plate_number, plate_number_enc, reid_id, zone_name, "
      "roi_index, entry_time, pay_status, total_amount "
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
  record["plate_number"] = resolveStoredPlate(query);
  record["reid_id"] = query.value("reid_id").toString();
  record["zone_name"] = query.value("zone_name").toString();
  record["roi_index"] = query.value("roi_index").toInt();
  record["entry_time"] = query.value("entry_time").toString();
  record["pay_status"] = query.value("pay_status").toString();
  record["total_amount"] = query.value("total_amount").toInt();
  return record;
}

QJsonObject ParkingRepository::findActiveByReidId(const QString &cameraKey,
                                                  const QString &reidId,
                                                  QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  const QString trimmedReid = normalizedReidId(reidId);
  if (!hasUsableReidId(trimmedReid)) {
    return QJsonObject();
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, object_id, plate_number, plate_number_enc, reid_id, zone_name, "
      "roi_index, entry_time, pay_status, total_amount "
      "FROM parking_logs WHERE camera_key = :camera_key "
      "AND reid_id = :reid_id AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":reid_id", trimmedReid);

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
  record["plate_number"] = resolveStoredPlate(query);
  record["reid_id"] = query.value("reid_id").toString();
  record["zone_name"] = query.value("zone_name").toString();
  record["roi_index"] = query.value("roi_index").toInt();
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

  // 1단계: ID 조회 (SELECT)
  QSqlQuery selectQuery(db);
  selectQuery.prepare(QStringLiteral(
      "SELECT id FROM parking_logs WHERE camera_key = :camera_key "
      "AND object_id = :object_id AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  selectQuery.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  selectQuery.bindValue(":object_id", objectId);

  if (!selectQuery.exec() || !selectQuery.next()) {
    return false;
  }
  int recordId = selectQuery.value(0).toInt();

  // 2단계: 직접 업데이트 (UPDATE)
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET plate_number = :plate_hash, plate_number_enc = :plate_enc WHERE id = :id"));
  query.bindValue(":plate_hash", plateLookupToken(plateNumber));
  query.bindValue(":plate_enc", encryptPlateNumber(plateNumber));
  query.bindValue(":id", recordId);

  if (!query.exec()) {
    const QString err = QStringLiteral("Update active plate error: ") +
                        query.lastError().text();
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
  if (!db.isOpen())
    return false;

  const QString trimmedReid = reidId.trimmed();
  if (!hasUsableReidId(trimmedReid)) {
    return false;
  }

  // 1단계: ID 조회 (SELECT)
  QSqlQuery selectQuery(db);
  selectQuery.prepare(QStringLiteral(
      "SELECT id FROM parking_logs WHERE camera_key = :camera_key "
      "AND reid_id = :reid_id AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  selectQuery.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  selectQuery.bindValue(":reid_id", trimmedReid);

  if (!selectQuery.exec() || !selectQuery.next()) {
    return false;
  }
  int recordId = selectQuery.value(0).toInt();

  // 2단계: 직접 업데이트 (UPDATE)
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET plate_number = :plate_hash, plate_number_enc = :plate_enc WHERE id = :id"));
  query.bindValue(":plate_hash", plateLookupToken(plateNumber));
  query.bindValue(":plate_enc", encryptPlateNumber(plateNumber));
  query.bindValue(":id", recordId);

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

bool ParkingRepository::updateActiveObjectIdByReidId(const QString &cameraKey,
                                                     const QString &reidId,
                                                     int objectId,
                                                     QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  const QString trimmedReid = normalizedReidId(reidId);
  if (!hasUsableReidId(trimmedReid)) {
    return false;
  }

  QSqlQuery selectQuery(db);
  selectQuery.prepare(QStringLiteral(
      "SELECT id, object_id FROM parking_logs WHERE camera_key = :camera_key "
      "AND reid_id = :reid_id AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  selectQuery.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  selectQuery.bindValue(":reid_id", trimmedReid);

  if (!selectQuery.exec() || !selectQuery.next()) {
    return false;
  }

  const int recordId = selectQuery.value(0).toInt();
  const int currentObjectId = selectQuery.value(1).toInt();
  if (currentObjectId == objectId) {
    return true;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET object_id = :object_id WHERE id = :id"));
  query.bindValue(":object_id", objectId);
  query.bindValue(":id", recordId);

  if (!query.exec()) {
    const QString err =
        QStringLiteral("Update active object by reid error: ") +
        query.lastError().text();
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
  if (!db.isOpen())
    return false;

  const QString trimmedReid = reidId.trimmed();
  if (!hasUsableReidId(trimmedReid)) {
    return false;
  }

  // 1단계: ID 조회 (SELECT)
  QSqlQuery selectQuery(db);
  selectQuery.prepare(QStringLiteral(
      "SELECT id FROM parking_logs WHERE camera_key = :camera_key "
      "AND object_id = :object_id AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  selectQuery.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  selectQuery.bindValue(":object_id", objectId);

  if (!selectQuery.exec() || !selectQuery.next()) {
    return false;
  }
  int recordId = selectQuery.value(0).toInt();

  // 2단계: 직접 업데이트 (UPDATE)
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET reid_id = :reid_id WHERE id = :id"));
  query.bindValue(":reid_id", trimmedReid);
  query.bindValue(":id", recordId);

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

QJsonObject ParkingRepository::findActiveByPlate(const QString &cameraKey,
                                                 const QString &plateNumber,
                                                 QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, object_id, plate_number, plate_number_enc, reid_id, zone_name, roi_index, "
      "entry_time, pay_status, total_amount "
      "FROM parking_logs WHERE camera_key = :camera_key "
      "AND plate_number = :plate AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":plate", plateLookupToken(plateNumber));

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
  record["plate_number"] = resolveStoredPlate(query);
  record["reid_id"] = query.value("reid_id").toString();
  record["zone_name"] = query.value("zone_name").toString();
  record["roi_index"] = query.value("roi_index").toInt();
  record["entry_time"] = query.value("entry_time").toString();
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

  const QString normalizedKey = normalizedCameraKey(cameraKey);
  QSqlQuery query(db);
  query.prepare(
      QStringLiteral("SELECT id, camera_key, object_id, plate_number, plate_number_enc, reid_id, "
                     "zone_name, roi_index, "
                     "entry_time, exit_time, pay_status, total_amount "
                     "FROM parking_logs WHERE camera_key = :camera_key "
                     "ORDER BY entry_time DESC LIMIT :limit"));
  query.bindValue(":camera_key", normalizedKey);
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
    row["plate_number"] = resolveStoredPlate(query);
    row["reid_id"] = query.value("reid_id").toString();
    row["zone_name"] = query.value("zone_name").toString();
    row["roi_index"] = query.value("roi_index").toInt();
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

  const QString normalizedKey = normalizedCameraKey(cameraKey);
  QSqlQuery query(db);
  query.prepare(
      QStringLiteral("SELECT id, camera_key, object_id, plate_number, reid_id, "
                     "plate_number_enc, "
                     "zone_name, roi_index, "
                     "entry_time, exit_time, pay_status, total_amount "
                     "FROM parking_logs WHERE camera_key = :camera_key "
                     "AND plate_number = :plate "
                     "ORDER BY entry_time DESC LIMIT 100"));
  query.bindValue(":camera_key", normalizedKey);
  query.bindValue(":plate", plateLookupToken(plate));

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
    row["plate_number"] = resolveStoredPlate(query);
    row["reid_id"] = query.value("reid_id").toString();
    row["zone_name"] = query.value("zone_name").toString();
    row["roi_index"] = query.value("roi_index").toInt();
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
  query.prepare(QStringLiteral("UPDATE parking_logs SET plate_number = :plate_hash, plate_number_enc = :plate_enc "
                               "WHERE id = :id AND camera_key = :camera_key"));
  query.bindValue(":plate_hash", plateLookupToken(newPlate));
  query.bindValue(":plate_enc", encryptPlateNumber(newPlate));
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

QList<QJsonObject> ParkingRepository::getAllLogs(const QString &cameraKey,
                                                 QString *errorMessage) const {
  QList<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();

  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = "Database is not open";
    return results;
  }

  const QString normalizedKey = normalizedCameraKey(cameraKey);
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, object_id, plate_number, plate_number_enc, reid_id, zone_name, roi_index, "
      "entry_time, exit_time, pay_status, total_amount "
      "FROM parking_logs WHERE camera_key = :camera_key "
      "ORDER BY entry_time DESC"));
  query.bindValue(":camera_key", normalizedKey);
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
    row["plate_number"] = resolveStoredPlate(query);
    row["reid_id"] = query.value("reid_id").toString();
    row["zone_name"] = query.value("zone_name").toString();
    row["roi_index"] = query.value("roi_index").toInt();
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

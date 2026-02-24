#include "parking/parkingrepository.h"
#include "database/databasecontext.h"
#include <QDebug>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {
const QString kDefaultCameraKey = QStringLiteral("camera");

QString normalizedCameraKey(const QString &cameraKey) {
  const QString trimmed = cameraKey.trimmed();
  return trimmed.isEmpty() ? kDefaultCameraKey : trimmed;
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
                     "  plate_number TEXT NOT NULL,"
                     "  roi_index INTEGER NOT NULL,"
                     "  entry_time TEXT NOT NULL,"
                     "  exit_time TEXT,"
                     "  pay_status TEXT DEFAULT 'Pending',"
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

  // 마이그레이션: 컬럼 추가 (존재하지 않을 경우)
  const QStringList newColumns = {
      "ALTER TABLE parking_logs ADD COLUMN camera_key TEXT NOT NULL DEFAULT "
      "'camera'",
      "ALTER TABLE parking_logs ADD COLUMN pay_status TEXT DEFAULT 'Pending'",
      "ALTER TABLE parking_logs ADD COLUMN bestshot_path TEXT",
      "ALTER TABLE parking_logs ADD COLUMN ocr_confidence REAL DEFAULT 0.0"};

  for (const QString &alterSql : newColumns) {
    QSqlQuery alterQuery(db);
    // 컬럼이 이미 존재하면 에러가 발생하겠지만 무시
    alterQuery.exec(alterSql);
  }

  QSqlQuery indexQuery(db);
  indexQuery.exec(QStringLiteral(
      "CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_entry "
      "ON parking_logs(camera_key, entry_time DESC)"));
  indexQuery.exec(QStringLiteral(
      "CREATE INDEX IF NOT EXISTS idx_parking_logs_camera_plate_active "
      "ON parking_logs(camera_key, plate_number, exit_time)"));

  return true;
}

int ParkingRepository::insertEntry(const QString &cameraKey,
                                   const QString &plateNumber, int roiIndex,
                                   const QDateTime &entryTime,
                                   QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return -1;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "INSERT INTO parking_logs (camera_key, plate_number, roi_index, "
      "entry_time, pay_status, ocr_confidence, bestshot_path) "
      "VALUES (:camera_key, :plate, :roi, :entry, 'Pending', 0.0, '')"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":plate", plateNumber);
  query.bindValue(":roi", roiIndex);
  query.bindValue(":entry", entryTime.toString(Qt::ISODate));

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
                                   QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET exit_time = :exit WHERE id = :id"));
  query.bindValue(":exit", exitTime.toString(Qt::ISODate));
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

QJsonObject ParkingRepository::findActiveByPlate(const QString &cameraKey,
                                                 const QString &plateNumber,
                                                 QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, plate_number, roi_index, entry_time "
      "FROM parking_logs WHERE camera_key = :camera_key "
      "AND plate_number = :plate AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
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
  record["plate_number"] = query.value("plate_number").toString();
  record["roi_index"] = query.value("roi_index").toInt();
  record["entry_time"] = query.value("entry_time").toString();
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
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, plate_number, roi_index, entry_time, exit_time "
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
    row["plate_number"] = query.value("plate_number").toString();
    row["roi_index"] = query.value("roi_index").toInt();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
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
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, plate_number, roi_index, entry_time, exit_time "
      "FROM parking_logs WHERE camera_key = :camera_key "
      "AND plate_number LIKE :plate "
      "ORDER BY entry_time DESC LIMIT 100"));
  query.bindValue(":camera_key", normalizedKey);
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
    row["plate_number"] = query.value("plate_number").toString();
    row["roi_index"] = query.value("roi_index").toInt();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
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
  query.bindValue(":plate", newPlate);
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

  const QString normalizedKey = normalizedCameraKey(cameraKey);
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, camera_key, plate_number, roi_index, entry_time, exit_time "
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
    row["plate_number"] = query.value("plate_number").toString();
    row["roi_index"] = query.value("roi_index").toInt();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
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

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
                     "  object_id INTEGER DEFAULT -1,"
                     "  plate_number TEXT DEFAULT ''," // NOT NULL 제거
                     "  zone_name TEXT DEFAULT '',"
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

  // 마이그레이션: plate_number의 NOT NULL 제약 조건 제거 확인
  query.prepare(QStringLiteral("PRAGMA table_info(parking_logs)"));
  if (query.exec()) {
    bool plateNumberNotNull = false;
    while (query.next()) {
      if (query.value(1).toString() == "plate_number" && query.value(3).toInt() == 1) {
        plateNumberNotNull = true;
        break;
      }
    }
    
    if (plateNumberNotNull) {
      qDebug() << "[DB] Migrating parking_logs to remove NOT NULL from plate_number";
      // SQLite에서 제약 조건을 바꾸려면 테이블을 새로 만들고 데이터를 복사해야 함
      const QStringList migrateSql = {
        "BEGIN TRANSACTION",
        "CREATE TABLE parking_logs_new ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  camera_key TEXT NOT NULL DEFAULT 'camera',"
        "  object_id INTEGER DEFAULT -1,"
        "  plate_number TEXT DEFAULT '',"
        "  zone_name TEXT DEFAULT '',"
        "  entry_time TEXT NOT NULL,"
        "  exit_time TEXT,"
        "  pay_status TEXT DEFAULT 'Pending',"
        "  bestshot_path TEXT,"
        "  ocr_confidence REAL DEFAULT 0.0,"
        "  created_at TEXT"
        ")",
        "INSERT INTO parking_logs_new SELECT * FROM parking_logs",
        "DROP TABLE parking_logs",
        "ALTER TABLE parking_logs_new RENAME TO parking_logs",
        "COMMIT"
      };
      
      for (const QString &s : migrateSql) {
        if (!query.exec(s)) {
          qWarning() << "[DB] Migration failed at:" << s << "Error:" << query.lastError().text();
          query.exec("ROLLBACK");
          break;
        }
      }
    }
  }

  // 마이그레이션: 컬럼 추가 (존재하지 않을 경우)
  const QStringList newColumns = {
      "ALTER TABLE parking_logs ADD COLUMN camera_key TEXT NOT NULL DEFAULT "
      "'camera'",
      "ALTER TABLE parking_logs ADD COLUMN pay_status TEXT DEFAULT 'Pending'",
      "ALTER TABLE parking_logs ADD COLUMN bestshot_path TEXT",
      "ALTER TABLE parking_logs ADD COLUMN ocr_confidence REAL DEFAULT 0.0",
      "ALTER TABLE parking_logs ADD COLUMN object_id INTEGER DEFAULT -1",
      "ALTER TABLE parking_logs ADD COLUMN zone_name TEXT DEFAULT ''"};

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
                                   const QString &plateNumber,
                                   const QDateTime &entryTime, int objectId,
                                   const QString &roiName,
                                   QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return -1;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "INSERT INTO parking_logs (camera_key, object_id, plate_number, "
      "zone_name, entry_time, pay_status, ocr_confidence, "
      "bestshot_path) "
      "VALUES (:camera_key, :obj_id, :plate, :zone_name, :entry, "
      "'Pending', 0.0, '')"));
  query.bindValue(":camera_key", normalizedCameraKey(cameraKey));
  query.bindValue(":obj_id", objectId);
  query.bindValue(":plate", plateNumber);
  query.bindValue(":zone_name", roiName);
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

QJsonObject
ParkingRepository::findActiveByPlate(const QString &cameraKey,
                                     const QString &plateNumber,
                                     QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, object_id, plate_number, zone_name, entry_time "
      "FROM parking_logs "
      "WHERE camera_key = :camera AND plate_number = :plate "
      "AND exit_time IS NULL "
      "ORDER BY id DESC LIMIT 1"));
  query.bindValue(":camera", normalizedCameraKey(cameraKey));
  query.bindValue(":plate", plateNumber);

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return QJsonObject();
  }

  if (query.next()) {
    QJsonObject obj;
    obj["id"] = query.value("id").toInt();
    obj["object_id"] = query.value("object_id").toInt();
    obj["plate_number"] = query.value("plate_number").toString();
    obj["zone_name"] = query.value("zone_name").toString();
    obj["entry_time"] = query.value("entry_time").toString();
    return obj;
  }

  return QJsonObject();
}

QJsonObject
ParkingRepository::findActiveByObjectId(const QString &cameraKey, int objectId,
                                        QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, object_id, plate_number, zone_name, entry_time "
      "FROM parking_logs "
      "WHERE camera_key = :camera AND object_id = :obj_id "
      "AND exit_time IS NULL "
      "ORDER BY id DESC LIMIT 1"));
  query.bindValue(":camera", normalizedCameraKey(cameraKey));
  query.bindValue(":obj_id", objectId);

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return QJsonObject();
  }

  if (query.next()) {
    QJsonObject obj;
    obj["id"] = query.value("id").toInt();
    obj["object_id"] = query.value("object_id").toInt();
    obj["plate_number"] = query.value("plate_number").toString();
    obj["zone_name"] = query.value("zone_name").toString();
    obj["entry_time"] = query.value("entry_time").toString();
    return obj;
  }

  return QJsonObject();
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
      "SELECT id, camera_key, object_id, plate_number, zone_name, "
      "entry_time, exit_time, pay_status "
      "FROM parking_logs WHERE camera_key = :camera_key "
      "ORDER BY id DESC LIMIT :limit"));
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
    row["plate_number"] = query.value("plate_number").toString();
    row["zone_name"] = query.value("zone_name").toString();
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
      "SELECT id, camera_key, object_id, plate_number, zone_name, "
      "entry_time, exit_time, pay_status "
      "FROM parking_logs WHERE camera_key = :camera_key "
      "AND plate_number LIKE :plate "
      "ORDER BY id DESC LIMIT 100"));
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
    row["object_id"] = query.value("object_id").toInt();
    row["plate_number"] = query.value("plate_number").toString();
    row["zone_name"] = query.value("zone_name").toString();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
    row["pay_status"] = query.value("pay_status").toString();
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
      "SELECT id, camera_key, object_id, plate_number, zone_name, "
      "entry_time, exit_time, pay_status "
      "FROM parking_logs WHERE camera_key = :camera_key "
      "ORDER BY id DESC"));
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
    row["plate_number"] = query.value("plate_number").toString();
    row["zone_name"] = query.value("zone_name").toString();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
    row["pay_status"] = query.value("pay_status").toString();
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

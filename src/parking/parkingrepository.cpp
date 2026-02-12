#include "parking/parkingrepository.h"
#include "database/databasecontext.h"
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

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
                     "  plate_number TEXT NOT NULL,"
                     "  roi_index INTEGER NOT NULL,"
                     "  entry_time TEXT NOT NULL,"
                     "  exit_time TEXT,"
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

  return true;
}

int ParkingRepository::insertEntry(const QString &plateNumber, int roiIndex,
                                   const QDateTime &entryTime,
                                   QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return -1;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "INSERT INTO parking_logs (plate_number, roi_index, entry_time) "
      "VALUES (:plate, :roi, :entry)"));
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

QJsonObject ParkingRepository::findActiveByPlate(const QString &plateNumber,
                                                 QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, plate_number, roi_index, entry_time "
      "FROM parking_logs WHERE plate_number = :plate AND exit_time IS NULL "
      "ORDER BY entry_time DESC LIMIT 1"));
  query.bindValue(":plate", plateNumber);

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  QJsonObject record;
  record["id"] = query.value("id").toInt();
  record["plate_number"] = query.value("plate_number").toString();
  record["roi_index"] = query.value("roi_index").toInt();
  record["entry_time"] = query.value("entry_time").toString();
  return record;
}

QVector<QJsonObject>
ParkingRepository::recentLogs(int limit, QString *errorMessage) const {
  QVector<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return results;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, plate_number, roi_index, entry_time, exit_time "
      "FROM parking_logs ORDER BY entry_time DESC LIMIT :limit"));
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
    row["plate_number"] = query.value("plate_number").toString();
    row["roi_index"] = query.value("roi_index").toInt();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
    results.append(row);
  }
  return results;
}

QVector<QJsonObject>
ParkingRepository::searchByPlate(const QString &plate,
                                 QString *errorMessage) const {
  QVector<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return results;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT id, plate_number, roi_index, entry_time, exit_time "
      "FROM parking_logs WHERE plate_number LIKE :plate "
      "ORDER BY entry_time DESC LIMIT 100"));
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
    row["plate_number"] = query.value("plate_number").toString();
    row["roi_index"] = query.value("roi_index").toInt();
    row["entry_time"] = query.value("entry_time").toString();
    row["exit_time"] = query.value("exit_time").toString();
    results.append(row);
  }
  return results;
}

bool ParkingRepository::updatePlate(int recordId, const QString &newPlate,
                                    QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "UPDATE parking_logs SET plate_number = :plate WHERE id = :id"));
  query.bindValue(":plate", newPlate);
  query.bindValue(":id", recordId);

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

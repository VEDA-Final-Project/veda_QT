#include "infrastructure/persistence/vehiclerepository.h"
#include "infrastructure/persistence/databasecontext.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

namespace {
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

VehicleRepository::VehicleRepository() {}

bool VehicleRepository::init(QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = "Database is not open";
    return false;
  }

  QSqlQuery query(db);
  const QString sql =
      QStringLiteral("CREATE TABLE IF NOT EXISTS vehicles ("
                     "  plate_number TEXT PRIMARY KEY,"
                     "  car_type TEXT,"
                     "  car_color TEXT,"
                     "  is_assigned INTEGER DEFAULT 0,"
                     "  reid_id TEXT,"
                     "  updated_at TEXT DEFAULT (datetime('now','localtime'))"
                     ")");

  if (!query.exec(sql)) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return false;
  }
  if (!ensureColumn(db, QStringLiteral("vehicles"), QStringLiteral("reid_id"),
                    QStringLiteral("TEXT"), errorMessage)) {
    return false;
  }
  return true;
}

bool VehicleRepository::upsertVehicle(const QString &plateNumber,
                                      const QString &carType,
                                      const QString &carColor, bool isAssigned,
                                      const QString &reidId,
                                      QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  const QString trimmedPlate = plateNumber.trimmed();
  if (trimmedPlate.isEmpty()) {
    return false;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "INSERT INTO vehicles (plate_number, car_type, car_color, is_assigned, "
      "reid_id, updated_at) "
      "VALUES (:plate, :type, :color, :assigned, :reid_id, "
      "datetime('now','localtime')) "
      "ON CONFLICT(plate_number) DO UPDATE SET "
      "car_type = excluded.car_type, "
      "car_color = excluded.car_color, "
      "is_assigned = excluded.is_assigned, "
      "reid_id = CASE WHEN excluded.reid_id != '' THEN excluded.reid_id "
      "ELSE vehicles.reid_id END, "
      "updated_at = excluded.updated_at"));

  query.bindValue(":plate", trimmedPlate);
  query.bindValue(":type", carType);
  query.bindValue(":color", carColor);
  query.bindValue(":assigned", isAssigned ? 1 : 0);
  query.bindValue(":reid_id", reidId.trimmed());

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    qWarning() << "[Vehicle] Upsert failed:" << query.lastError().text();
    return false;
  }
  return true;
}

QJsonObject VehicleRepository::findByPlate(const QString &plateNumber,
                                           QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QJsonObject();

  QSqlQuery query(db);
  query.prepare("SELECT plate_number, car_type, car_color, is_assigned, "
                "reid_id FROM vehicles WHERE plate_number = :plate");
  query.bindValue(":plate", plateNumber);

  if (!query.exec() || !query.next()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return QJsonObject();
  }

  QJsonObject record;
  record["plate_number"] = query.value("plate_number").toString();
  record["car_type"] = query.value("car_type").toString();
  record["car_color"] = query.value("car_color").toString();
  record["is_assigned"] = query.value("is_assigned").toBool();
  record["reid_id"] = query.value("reid_id").toString();
  return record;
}

QVector<QJsonObject>
VehicleRepository::getAllVehicles(QString *errorMessage) const {
  QVector<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return results;

  QSqlQuery query(db);
  if (!query.exec(QStringLiteral("SELECT plate_number, car_type, car_color, "
                                 "is_assigned, reid_id, updated_at "
                                 "FROM vehicles "
                                 "ORDER BY updated_at DESC"))) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["plate_number"] = query.value("plate_number").toString();
    row["car_type"] = query.value("car_type").toString();
    row["car_color"] = query.value("car_color").toString();
    row["is_assigned"] = query.value("is_assigned").toBool();
    row["reid_id"] = query.value("reid_id").toString();
    row["updated_at"] = query.value("updated_at").toString();
    results.append(row);
  }
  return results;
}

bool VehicleRepository::deleteVehicle(const QString &plateNumber,
                                      QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  QSqlQuery query(db);
  query.prepare(
      QStringLiteral("DELETE FROM vehicles WHERE plate_number = :plate"));
  query.bindValue(":plate", plateNumber);

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return false;
  }
  return true;
}

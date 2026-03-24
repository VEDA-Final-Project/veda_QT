#include "infrastructure/persistence/vehiclerepository.h"
#include "infrastructure/persistence/databasecontext.h"
#include "infrastructure/security/dataprotection.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

VehicleRepository::VehicleRepository() {}

namespace {
QString encryptPlateNumber(const QString &plateNumber) {
  return DataProtection::instance().encryptString(plateNumber.trimmed());
}

QString plateLookupToken(const QString &plateNumber) {
  return DataProtection::instance().lookupToken(QStringLiteral("vehicle_plate"),
                                                plateNumber);
}

QString resolveStoredPlate(const QSqlQuery &query) {
  const QString encryptedPlate = query.value("plate_number_enc").toString();
  if (!encryptedPlate.isEmpty()) {
    return DataProtection::instance().decryptString(encryptedPlate);
  }
  return query.value("plate_number").toString().trimmed();
}

bool migrateVehiclePlateStorage(QSqlDatabase db, QString *errorMessage) {
  QSqlQuery query(db);
  if (!query.exec(
          QStringLiteral("SELECT plate_number, plate_number_enc FROM vehicles"))) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  while (query.next()) {
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
        "UPDATE vehicles SET plate_number = :plate_hash, "
        "plate_number_enc = :plate_enc WHERE plate_number = :legacy_plate"));
    updateQuery.bindValue(":plate_hash", lookupToken);
    updateQuery.bindValue(":plate_enc", encryptedPlate);
    updateQuery.bindValue(":legacy_plate", currentStored);

    if (!updateQuery.exec()) {
      if (errorMessage) {
        *errorMessage = updateQuery.lastError().text();
      }
      return false;
    }
  }

  return true;
}
}

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
                     "  plate_number_enc TEXT NOT NULL DEFAULT '',"
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

  QSqlQuery alterQuery(db);
  alterQuery.exec(QStringLiteral(
      "ALTER TABLE vehicles ADD COLUMN plate_number_enc TEXT NOT NULL DEFAULT ''"));

  if (!migrateVehiclePlateStorage(db, errorMessage)) {
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

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "INSERT INTO vehicles (plate_number, plate_number_enc, car_type, car_color, is_assigned, "
      "reid_id, updated_at) "
      "VALUES (:plate_hash, :plate_enc, :type, :color, :assigned, :reid_id, datetime('now','localtime')) "
      "ON CONFLICT(plate_number) DO UPDATE SET "
      "plate_number_enc = excluded.plate_number_enc, "
      "car_type = excluded.car_type, "
      "car_color = excluded.car_color, "
      "is_assigned = excluded.is_assigned, "
      "reid_id = CASE WHEN excluded.reid_id != '' THEN excluded.reid_id ELSE vehicles.reid_id END, "
      "updated_at = excluded.updated_at"));

  query.bindValue(":plate_hash", plateLookupToken(plateNumber));
  query.bindValue(":plate_enc", encryptPlateNumber(plateNumber));
  query.bindValue(":type", carType);
  query.bindValue(":color", carColor);
  query.bindValue(":assigned", isAssigned ? 1 : 0);
  query.bindValue(":reid_id", reidId);

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
  query.prepare("SELECT plate_number, plate_number_enc, car_type, car_color, is_assigned FROM "
                "vehicles WHERE plate_number = :plate");
  query.bindValue(":plate", plateLookupToken(plateNumber));

  if (!query.exec() || !query.next()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return QJsonObject();
  }

  QJsonObject record;
  record["plate_number"] = resolveStoredPlate(query);
  record["car_type"] = query.value("car_type").toString();
  record["car_color"] = query.value("car_color").toString();
  record["is_assigned"] = query.value("is_assigned").toBool();
  return record;
}

QVector<QJsonObject>
VehicleRepository::getAllVehicles(QString *errorMessage) const {
  QVector<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return results;

  QSqlQuery query(db);
  if (!query.exec(QStringLiteral("SELECT plate_number, plate_number_enc, car_type, car_color, "
                                 "is_assigned, updated_at FROM vehicles "
                                 "ORDER BY updated_at DESC"))) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["plate_number"] = resolveStoredPlate(query);
    row["car_type"] = query.value("car_type").toString();
    row["car_color"] = query.value("car_color").toString();
    row["is_assigned"] = query.value("is_assigned").toBool();
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
  query.bindValue(":plate", plateLookupToken(plateNumber));

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return false;
  }
  return true;
}

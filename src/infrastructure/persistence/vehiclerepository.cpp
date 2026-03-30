#include "infrastructure/persistence/vehiclerepository.h"

#include "infrastructure/persistence/databasecontext.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
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

QString primaryPlateSelectSql() {
  return QStringLiteral(
      "COALESCE("
      "(SELECT vp.plate_number FROM vehicle_plates vp "
      " WHERE vp.vehicle_id = v.vehicle_id AND vp.is_primary = 1 "
      " ORDER BY datetime(vp.created_at) DESC, vp.plate_id DESC LIMIT 1),"
      "(SELECT vp.plate_number FROM vehicle_plates vp "
      " WHERE vp.vehicle_id = v.vehicle_id "
      " ORDER BY datetime(vp.created_at) DESC, vp.plate_id DESC LIMIT 1),"
      "''"
      ")");
}

QJsonObject vehicleFromQuery(const QSqlQuery &query) {
  QJsonObject record;
  record["vehicle_id"] = query.value("vehicle_id").toInt();
  record["plate_number"] = query.value("plate_number").toString();
  record["reid_id"] = query.value("reid_id").toString();
  record["car_type"] = query.value("car_type").toString();
  record["car_color"] = query.value("car_color").toString();
  record["is_assigned"] = false;
  record["updated_at"] = query.value("updated_at").toString();
  return record;
}
} // namespace

VehicleRepository::VehicleRepository() {}

bool VehicleRepository::init(QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Database is not open");
    }
    return false;
  }

  if (!execSql(
          db, QStringLiteral("CREATE TABLE IF NOT EXISTS vehicles ("
                             "  vehicle_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "  current_reid_id TEXT,"
                             "  car_type TEXT,"
                             "  car_color TEXT,"
                             "  updated_at TEXT NOT NULL DEFAULT "
                             "(datetime('now','localtime'))"
                             ")"),
          errorMessage)) {
    return false;
  }

  if (!execSql(
          db, QStringLiteral("CREATE TABLE IF NOT EXISTS vehicle_plates ("
                             "  plate_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "  vehicle_id INTEGER NOT NULL,"
                             "  plate_number TEXT NOT NULL,"
                             "  is_primary INTEGER NOT NULL DEFAULT 0,"
                             "  created_at TEXT NOT NULL DEFAULT "
                             "(datetime('now','localtime')),"
                             "  FOREIGN KEY (vehicle_id) REFERENCES "
                             "vehicles(vehicle_id) ON DELETE CASCADE"
                             ")"),
          errorMessage)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS "
                              "idx_vehicles_current_reid "
                              "ON vehicles(current_reid_id) "
                              "WHERE current_reid_id IS NOT NULL AND "
                              "TRIM(current_reid_id) != ''"),
               errorMessage)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS "
                              "idx_vehicle_plates_plate_number "
                              "ON vehicle_plates(plate_number)"),
               errorMessage)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS "
                              "idx_vehicle_plates_primary_per_vehicle "
                              "ON vehicle_plates(vehicle_id) "
                              "WHERE is_primary = 1"),
               errorMessage)) {
    return false;
  }

  return true;
}

int VehicleRepository::findVehicleIdByPlate(const QString &plateNumber,
                                            QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Database is not open");
    }
    return -1;
  }

  const QString normalizedPlate = normalizedPlateNumber(plateNumber);
  if (normalizedPlate.isEmpty()) {
    return -1;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT vehicle_id FROM vehicle_plates WHERE plate_number = :plate "
      "LIMIT 1"));
  query.bindValue(":plate", normalizedPlate);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return -1;
  }

  return query.next() ? query.value(0).toInt() : -1;
}

int VehicleRepository::findVehicleIdByReid(const QString &reidId,
                                           QString *errorMessage) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Database is not open");
    }
    return -1;
  }

  const QString normalizedReid = normalizedReidId(reidId);
  if (normalizedReid.isEmpty()) {
    return -1;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT vehicle_id FROM vehicles WHERE current_reid_id = :reid "
      "LIMIT 1"));
  query.bindValue(":reid", normalizedReid);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return -1;
  }

  return query.next() ? query.value(0).toInt() : -1;
}

int VehicleRepository::ensureVehicle(const QString &plateNumber,
                                     const QString &carType,
                                     const QString &carColor,
                                     const QString &reidId,
                                     QString *errorMessage) {
  if (!init(errorMessage)) {
    return -1;
  }

  QSqlDatabase db = DatabaseContext::database();
  const QString normalizedPlate = normalizedPlateNumber(plateNumber);
  const QString normalizedReid = normalizedReidId(reidId);

  int vehicleId = findVehicleIdByPlate(normalizedPlate, errorMessage);
  if (vehicleId < 0) {
    vehicleId = findVehicleIdByReid(normalizedReid, errorMessage);
  }

  if (!db.transaction()) {
    if (errorMessage) {
      *errorMessage = db.lastError().text();
    }
    return -1;
  }

  auto rollback = [&db]() { db.rollback(); };

  if (vehicleId < 0) {
    QSqlQuery insertVehicle(db);
    insertVehicle.prepare(QStringLiteral(
        "INSERT INTO vehicles (current_reid_id, car_type, car_color, updated_at) "
        "VALUES (:reid, :type, :color, datetime('now','localtime'))"));
    insertVehicle.bindValue(":reid",
                            normalizedReid.isEmpty() ? QVariant()
                                                     : QVariant(normalizedReid));
    insertVehicle.bindValue(":type", carType.trimmed());
    insertVehicle.bindValue(":color", carColor.trimmed());

    if (!insertVehicle.exec()) {
      if (errorMessage) {
        *errorMessage = insertVehicle.lastError().text();
      }
      rollback();
      return -1;
    }

    vehicleId = insertVehicle.lastInsertId().toInt();
  } else {
    QSqlQuery updateVehicle(db);
    updateVehicle.prepare(QStringLiteral(
        "UPDATE vehicles "
        "SET current_reid_id = CASE "
        "      WHEN :reid IS NOT NULL AND TRIM(:reid) != '' THEN :reid "
        "      ELSE current_reid_id END, "
        "    car_type = CASE "
        "      WHEN TRIM(:type) != '' THEN :type ELSE car_type END, "
        "    car_color = CASE "
        "      WHEN TRIM(:color) != '' THEN :color ELSE car_color END, "
        "    updated_at = datetime('now','localtime') "
        "WHERE vehicle_id = :vehicle_id"));
    updateVehicle.bindValue(":reid",
                            normalizedReid.isEmpty() ? QVariant()
                                                     : QVariant(normalizedReid));
    updateVehicle.bindValue(":type", carType.trimmed());
    updateVehicle.bindValue(":color", carColor.trimmed());
    updateVehicle.bindValue(":vehicle_id", vehicleId);

    if (!updateVehicle.exec()) {
      if (errorMessage) {
        *errorMessage = updateVehicle.lastError().text();
      }
      rollback();
      return -1;
    }
  }

  if (!normalizedPlate.isEmpty()) {
    QSqlQuery clearPrimary(db);
    clearPrimary.prepare(QStringLiteral(
        "UPDATE vehicle_plates SET is_primary = 0 WHERE vehicle_id = :vehicle_id"));
    clearPrimary.bindValue(":vehicle_id", vehicleId);
    if (!clearPrimary.exec()) {
      if (errorMessage) {
        *errorMessage = clearPrimary.lastError().text();
      }
      rollback();
      return -1;
    }

    QSqlQuery upsertPlate(db);
    upsertPlate.prepare(QStringLiteral(
        "INSERT INTO vehicle_plates "
        "(vehicle_id, plate_number, is_primary, created_at) "
        "VALUES (:vehicle_id, :plate, 1, datetime('now','localtime')) "
        "ON CONFLICT(plate_number) DO UPDATE SET "
        "  vehicle_id = excluded.vehicle_id, "
        "  is_primary = excluded.is_primary"));
    upsertPlate.bindValue(":vehicle_id", vehicleId);
    upsertPlate.bindValue(":plate", normalizedPlate);
    if (!upsertPlate.exec()) {
      if (errorMessage) {
        *errorMessage = upsertPlate.lastError().text();
      }
      rollback();
      return -1;
    }
  }

  if (!db.commit()) {
    if (errorMessage) {
      *errorMessage = db.lastError().text();
    }
    rollback();
    return -1;
  }

  return vehicleId;
}

bool VehicleRepository::upsertVehicle(const QString &plateNumber,
                                      const QString &carType,
                                      const QString &carColor, bool isAssigned,
                                      const QString &reidId,
                                      QString *errorMessage) {
  Q_UNUSED(isAssigned);
  return ensureVehicle(plateNumber, carType, carColor, reidId, errorMessage) >
         0;
}

QJsonObject VehicleRepository::findByPlate(const QString &plateNumber,
                                           QString *errorMessage) const {
  if (!const_cast<VehicleRepository *>(this)->init(errorMessage)) {
    return QJsonObject();
  }

  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  const QString sql = QStringLiteral(
      "SELECT v.vehicle_id, %1 AS plate_number, "
      "COALESCE(v.current_reid_id, '') AS reid_id, "
      "v.car_type, v.car_color, v.updated_at "
      "FROM vehicles v "
      "WHERE v.vehicle_id = ("
      "  SELECT vehicle_id FROM vehicle_plates WHERE plate_number = :plate "
      "  LIMIT 1"
      ") LIMIT 1")
                          .arg(primaryPlateSelectSql());
  query.prepare(sql);
  query.bindValue(":plate", normalizedPlateNumber(plateNumber));

  if (!query.exec() || !query.next()) {
    if (errorMessage && query.lastError().isValid()) {
      *errorMessage = query.lastError().text();
    }
    return QJsonObject();
  }

  return vehicleFromQuery(query);
}

QVector<QJsonObject>
VehicleRepository::getAllVehicles(QString *errorMessage) const {
  QVector<QJsonObject> results;
  if (!const_cast<VehicleRepository *>(this)->init(errorMessage)) {
    return results;
  }

  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  const QString sql = QStringLiteral(
      "SELECT v.vehicle_id, %1 AS plate_number, "
      "COALESCE(v.current_reid_id, '') AS reid_id, "
      "v.car_type, v.car_color, v.updated_at "
      "FROM vehicles v "
      "ORDER BY datetime(v.updated_at) DESC, v.vehicle_id DESC")
                          .arg(primaryPlateSelectSql());

  if (!query.exec(sql)) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return results;
  }

  while (query.next()) {
    results.append(vehicleFromQuery(query));
  }
  return results;
}

bool VehicleRepository::deleteVehicle(const QString &plateNumber,
                                      QString *errorMessage) {
  if (!init(errorMessage)) {
    return false;
  }

  const int vehicleId = findVehicleIdByPlate(plateNumber, errorMessage);
  if (vehicleId < 0) {
    if (errorMessage && (errorMessage->isEmpty())) {
      *errorMessage = QStringLiteral("Vehicle not found");
    }
    return false;
  }

  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  query.prepare(
      QStringLiteral("DELETE FROM vehicles WHERE vehicle_id = :vehicle_id"));
  query.bindValue(":vehicle_id", vehicleId);

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  return query.numRowsAffected() > 0;
}

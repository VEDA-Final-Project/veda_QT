#include "infrastructure/persistence/userrepository.h"

#include "infrastructure/persistence/databasecontext.h"
#include "infrastructure/persistence/vehiclerepository.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

namespace {
QString normalizedPlateNumber(const QString &plateNumber) {
  return plateNumber.trimmed();
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

QString plateForChatSubquery() {
  return QStringLiteral(
      "COALESCE("
      "(SELECT vp.plate_number "
      " FROM user_vehicles uv "
      " JOIN vehicle_plates vp ON vp.vehicle_id = uv.vehicle_id "
      " WHERE uv.chat_id = tu.chat_id AND vp.is_primary = 1 "
      " ORDER BY datetime(uv.created_at) DESC, uv.id DESC LIMIT 1),"
      "(SELECT vp.plate_number "
      " FROM user_vehicles uv "
      " JOIN vehicle_plates vp ON vp.vehicle_id = uv.vehicle_id "
      " WHERE uv.chat_id = tu.chat_id "
      " ORDER BY datetime(uv.created_at) DESC, uv.id DESC, vp.plate_id DESC "
      " LIMIT 1),"
      "''"
      ")");
}

QJsonObject userFromQuery(const QSqlQuery &query) {
  QJsonObject row;
  row["chat_id"] = query.value("chat_id").toString();
  row["plate_number"] = query.value("plate_number").toString();
  row["name"] = query.value("name").toString();
  row["phone"] = query.value("phone").toString();
  row["payment_info"] = query.value("payment_info").toString();
  row["created_at"] = query.value("created_at").toString();
  return row;
}

bool upsertUserAndMapping(QSqlDatabase &db, const QString &chatId,
                          const QString &plateNumber, const QString &name,
                          const QString &phone, const QString &paymentInfo,
                          QString *errorMessage) {
  VehicleRepository vehicleRepo;
  int vehicleId = -1;
  const QString normalizedPlate = normalizedPlateNumber(plateNumber);
  if (!normalizedPlate.isEmpty()) {
    vehicleId = vehicleRepo.ensureVehicle(normalizedPlate, QString(), QString(),
                                          QString(), errorMessage);
    if (vehicleId < 0) {
      return false;
    }
  }

  if (!db.transaction()) {
    if (errorMessage) {
      *errorMessage = db.lastError().text();
    }
    return false;
  }

  auto rollback = [&db]() { db.rollback(); };

  QSqlQuery upsertUser(db);
  upsertUser.prepare(QStringLiteral(
      "INSERT INTO telegram_users (chat_id, name, phone, payment_info, created_at) "
      "VALUES (:chat_id, :name, :phone, :payment, datetime('now','localtime')) "
      "ON CONFLICT(chat_id) DO UPDATE SET "
      "  name = excluded.name, "
      "  phone = excluded.phone, "
      "  payment_info = excluded.payment_info"));
  upsertUser.bindValue(":chat_id", chatId);
  upsertUser.bindValue(":name", name);
  upsertUser.bindValue(":phone", phone);
  upsertUser.bindValue(":payment", paymentInfo);

  if (!upsertUser.exec()) {
    if (errorMessage) {
      *errorMessage = upsertUser.lastError().text();
    }
    rollback();
    return false;
  }

  QSqlQuery clearMappings(db);
  clearMappings.prepare(
      QStringLiteral("DELETE FROM user_vehicles WHERE chat_id = :chat_id"));
  clearMappings.bindValue(":chat_id", chatId);
  if (!clearMappings.exec()) {
    if (errorMessage) {
      *errorMessage = clearMappings.lastError().text();
    }
    rollback();
    return false;
  }

  if (vehicleId > 0) {
    QSqlQuery insertMapping(db);
    insertMapping.prepare(QStringLiteral(
        "INSERT INTO user_vehicles (chat_id, vehicle_id, created_at) "
        "VALUES (:chat_id, :vehicle_id, datetime('now','localtime'))"));
    insertMapping.bindValue(":chat_id", chatId);
    insertMapping.bindValue(":vehicle_id", vehicleId);
    if (!insertMapping.exec()) {
      if (errorMessage) {
        *errorMessage = insertMapping.lastError().text();
      }
      rollback();
      return false;
    }
  }

  if (!db.commit()) {
    if (errorMessage) {
      *errorMessage = db.lastError().text();
    }
    rollback();
    return false;
  }

  return true;
}
} // namespace

UserRepository::UserRepository() {}

bool UserRepository::init(QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Database is not open");
    }
    return false;
  }

  if (!execSql(
          db, QStringLiteral("CREATE TABLE IF NOT EXISTS telegram_users ("
                             "  chat_id TEXT PRIMARY KEY,"
                             "  name TEXT,"
                             "  phone TEXT,"
                             "  payment_info TEXT,"
                             "  created_at TEXT NOT NULL DEFAULT "
                             "(datetime('now','localtime'))"
                             ")"),
          errorMessage)) {
    return false;
  }

  if (!execSql(
          db, QStringLiteral("CREATE TABLE IF NOT EXISTS user_vehicles ("
                             "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "  chat_id TEXT NOT NULL,"
                             "  vehicle_id INTEGER NOT NULL,"
                             "  created_at TEXT NOT NULL DEFAULT "
                             "(datetime('now','localtime')),"
                             "  FOREIGN KEY (chat_id) REFERENCES "
                             "telegram_users(chat_id) ON DELETE CASCADE,"
                             "  FOREIGN KEY (vehicle_id) REFERENCES "
                             "vehicles(vehicle_id) ON DELETE CASCADE"
                             ")"),
          errorMessage)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS "
                              "idx_user_vehicles_chat_vehicle "
                              "ON user_vehicles(chat_id, vehicle_id)"),
               errorMessage)) {
    return false;
  }

  return true;
}

bool UserRepository::registerUser(const QString &chatId,
                                  const QString &plateNumber,
                                  const QString &name, const QString &phone,
                                  const QString &paymentInfo,
                                  QString *errorMessage) {
  if (!init(errorMessage)) {
    return false;
  }

  QSqlDatabase db = DatabaseContext::database();
  return upsertUserAndMapping(db, chatId.trimmed(),
                              plateNumber, name.trimmed(), phone.trimmed(),
                              paymentInfo.trimmed(), errorMessage);
}

bool UserRepository::updateUser(const QString &chatId,
                                const QString &plateNumber, const QString &name,
                                const QString &phone,
                                const QString &paymentInfo,
                                QString *errorMessage) {
  return registerUser(chatId, plateNumber, name, phone, paymentInfo,
                      errorMessage);
}

bool UserRepository::addUser(const QString &chatId, const QString &plateNumber,
                             const QString &name, const QString &phone,
                             const QString &paymentInfo,
                             QString *errorMessage) {
  return registerUser(chatId, plateNumber, name, phone, paymentInfo,
                      errorMessage);
}

QMap<QString, QString>
UserRepository::getAllUsers(QString *errorMessage) const {
  QMap<QString, QString> users;
  const QVector<QJsonObject> detailed =
      getAllUsersFull(errorMessage);
  for (const QJsonObject &row : detailed) {
    users.insert(row["chat_id"].toString(), row["plate_number"].toString());
  }
  return users;
}

QString UserRepository::findChatIdByPlate(const QString &plateNumber) const {
  QString error;
  if (!const_cast<UserRepository *>(this)->init(&error)) {
    return QString();
  }

  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT uv.chat_id "
      "FROM user_vehicles uv "
      "JOIN vehicle_plates vp ON vp.vehicle_id = uv.vehicle_id "
      "WHERE vp.plate_number = :plate "
      "ORDER BY datetime(uv.created_at) DESC, uv.id DESC LIMIT 1"));
  query.bindValue(":plate", normalizedPlateNumber(plateNumber));

  if (query.exec() && query.next()) {
    return query.value(0).toString();
  }
  return QString();
}

QString UserRepository::findPlateByChatId(const QString &chatId) const {
  QString error;
  const QVector<QJsonObject> detailed = getAllUsersFull(&error);
  for (const QJsonObject &row : detailed) {
    if (row["chat_id"].toString() == chatId.trimmed()) {
      return row["plate_number"].toString();
    }
  }
  return QString();
}

QVector<QJsonObject>
UserRepository::getAllUsersFull(QString *errorMessage) const {
  QVector<QJsonObject> results;
  if (!const_cast<UserRepository *>(this)->init(errorMessage)) {
    return results;
  }

  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  const QString sql = QStringLiteral(
      "SELECT tu.chat_id, %1 AS plate_number, "
      "tu.name, tu.phone, tu.payment_info, tu.created_at "
      "FROM telegram_users tu "
      "ORDER BY datetime(tu.created_at) DESC, tu.chat_id ASC")
                          .arg(plateForChatSubquery());

  if (!query.exec(sql)) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return results;
  }

  while (query.next()) {
    results.append(userFromQuery(query));
  }
  return results;
}

bool UserRepository::deleteUser(const QString &chatId, QString *errorMessage) {
  if (!init(errorMessage)) {
    return false;
  }

  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  query.prepare(
      QStringLiteral("DELETE FROM telegram_users WHERE chat_id = :chatId"));
  query.bindValue(":chatId", chatId.trimmed());

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }
  return query.numRowsAffected() > 0;
}

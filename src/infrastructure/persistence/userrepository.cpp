#include "infrastructure/persistence/userrepository.h"
#include "infrastructure/persistence/databasecontext.h"
#include "infrastructure/security/dataprotection.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

UserRepository::UserRepository() {}

namespace {
QString encryptUserField(const QString &value) {
  return DataProtection::instance().encryptString(value.trimmed());
}

QString decryptUserField(const QString &value) {
  return DataProtection::instance().decryptString(value);
}

QString encryptPlateNumber(const QString &plateNumber) {
  return DataProtection::instance().encryptString(plateNumber.trimmed());
}

QString plateLookupToken(const QString &plateNumber) {
  return DataProtection::instance().lookupToken(QStringLiteral("telegram_plate"),
                                                plateNumber);
}

QString resolveStoredPlate(const QSqlQuery &query) {
  const QString encryptedPlate = query.value("plate_number_enc").toString();
  if (!encryptedPlate.isEmpty()) {
    return DataProtection::instance().decryptString(encryptedPlate);
  }
  return query.value("plate_number").toString().trimmed();
}

bool migrateUserPlateStorage(QSqlDatabase db, QString *errorMessage) {
  QSqlQuery query(db);
  if (!query.exec(QStringLiteral(
          "SELECT chat_id, plate_number, plate_number_enc FROM telegram_users"))) {
    if (errorMessage) {
      *errorMessage = query.lastError().text();
    }
    return false;
  }

  while (query.next()) {
    const QString chatId = query.value("chat_id").toString();
    const QString resolvedPlate = resolveStoredPlate(query);
    const QString lookupToken = plateLookupToken(resolvedPlate);
    const QString encryptedPlate = encryptPlateNumber(resolvedPlate);
    const QString currentStored = query.value("plate_number").toString();
    const QString currentEncrypted = query.value("plate_number_enc").toString();

    if (currentStored == lookupToken && currentEncrypted == encryptedPlate) {
      continue;
    }

    QSqlQuery updateQuery(db);
    updateQuery.prepare(QStringLiteral(
        "UPDATE telegram_users SET plate_number = :plate_hash, "
        "plate_number_enc = :plate_enc WHERE chat_id = :chat_id"));
    updateQuery.bindValue(":plate_hash", lookupToken);
    updateQuery.bindValue(":plate_enc", encryptedPlate);
    updateQuery.bindValue(":chat_id", chatId);

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

bool UserRepository::init(QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = QStringLiteral("Database is not open");
    return false;
  }

  QSqlQuery query(db);
  const QString sql =
      QStringLiteral("CREATE TABLE IF NOT EXISTS telegram_users ("
                     "  chat_id TEXT PRIMARY KEY,"
                     "  plate_number TEXT NOT NULL DEFAULT '',"
                     "  plate_number_enc TEXT NOT NULL DEFAULT '',"
                     "  name TEXT,"
                     "  phone TEXT,"
                     "  payment_info TEXT,"
                     "  created_at TEXT DEFAULT (datetime('now','localtime'))"
                     ")");

  if (!query.exec(sql)) {
    const QString err =
        QStringLiteral("Failed to create telegram_users table: ") +
        query.lastError().text();
    qWarning() << err;
    if (errorMessage)
      *errorMessage = err;
    return false;
  }

  // 마이그레이션: 신규 컬럼 추가
  const QStringList newColumns = {
      "ALTER TABLE telegram_users ADD COLUMN plate_number_enc TEXT NOT NULL DEFAULT ''",
      "ALTER TABLE telegram_users ADD COLUMN name TEXT",
      "ALTER TABLE telegram_users ADD COLUMN phone TEXT",
      "ALTER TABLE telegram_users ADD COLUMN payment_info TEXT"};

  for (const QString &alterSql : newColumns) {
    QSqlQuery alterQuery(db);
    alterQuery.exec(alterSql);
  }

  if (!migrateUserPlateStorage(db, errorMessage)) {
    return false;
  }

  return true;
}

bool UserRepository::registerUser(const QString &chatId,
                                  const QString &plateNumber,
                                  const QString &name, const QString &phone,
                                  const QString &paymentInfo,
                                  QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  query.prepare(
      QStringLiteral("INSERT OR REPLACE INTO telegram_users (chat_id, "
                     "plate_number, plate_number_enc, name, phone, "
                     "payment_info, created_at) "
                     "VALUES (:chat_id, :plate_hash, :plate_enc, :name, :phone, :payment, "
                     "datetime('now','localtime'))"));
  query.bindValue(":chat_id", chatId);
  query.bindValue(":plate_hash", plateLookupToken(plateNumber));
  query.bindValue(":plate_enc", encryptPlateNumber(plateNumber));
  query.bindValue(":name", encryptUserField(name));
  query.bindValue(":phone", encryptUserField(phone));
  query.bindValue(":payment", encryptUserField(paymentInfo));

  if (!query.exec()) {
    const QString err =
        QStringLiteral("Failed to register user: ") + query.lastError().text();
    qWarning() << err;
    if (errorMessage)
      *errorMessage = err;
    return false;
  }
  return true;
}

bool UserRepository::updateUser(const QString &chatId,
                                const QString &plateNumber, const QString &name,
                                const QString &phone,
                                const QString &paymentInfo,
                                QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  query.prepare(
      QStringLiteral("UPDATE telegram_users SET plate_number=:plate_hash, "
                     "plate_number_enc=:plate_enc, name=:name, phone=:phone, payment_info=:payment "
                     "WHERE chat_id=:chat_id"));
  query.bindValue(":chat_id", chatId);
  query.bindValue(":plate_hash", plateLookupToken(plateNumber));
  query.bindValue(":plate_enc", encryptPlateNumber(plateNumber));
  query.bindValue(":name", encryptUserField(name));
  query.bindValue(":phone", encryptUserField(phone));
  query.bindValue(":payment", encryptUserField(paymentInfo));
  if (!query.exec()) {
    const QString err =
        QStringLiteral("Failed to update user: ") + query.lastError().text();
    qWarning() << err;
    if (errorMessage)
      *errorMessage = err;
    return false;
  }
  return true;
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
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return users;

  QSqlQuery query(db);
  if (!query.exec(QStringLiteral(
          "SELECT chat_id, plate_number, plate_number_enc FROM telegram_users"))) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return users;
  }

  while (query.next()) {
    users.insert(query.value("chat_id").toString(), resolveStoredPlate(query));
  }
  return users;
}

bool UserRepository::deleteUser(const QString &chatId, QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  QSqlQuery query(db);
  query.prepare(
      QStringLiteral("DELETE FROM telegram_users WHERE chat_id = :chatId"));
  query.bindValue(":chatId", chatId);

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return false;
  }
  return true;
}

QVector<QJsonObject>
UserRepository::getAllUsersFull(QString *errorMessage) const {
  QVector<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return results;

  QSqlQuery query(db);
  if (!query.exec(QStringLiteral("SELECT chat_id, plate_number, plate_number_enc, name, phone, "
                                 "payment_info, created_at FROM telegram_users "
                                 "ORDER BY created_at DESC"))) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["chat_id"] = query.value("chat_id").toString();
    row["plate_number"] = resolveStoredPlate(query);
    row["name"] = decryptUserField(query.value("name").toString());
    row["phone"] = decryptUserField(query.value("phone").toString());
    row["payment_info"] =
        decryptUserField(query.value("payment_info").toString());
    row["created_at"] = query.value("created_at").toString();
    results.append(row);
  }
  return results;
}

QString UserRepository::findChatIdByPlate(const QString &plateNumber) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QString();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT chat_id FROM telegram_users WHERE plate_number = :plate"));
  query.bindValue(":plate", plateLookupToken(plateNumber));

  if (query.exec() && query.next()) {
    return query.value(0).toString();
  }
  return QString();
}

QString UserRepository::findPlateByChatId(const QString &chatId) const {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return QString();

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "SELECT plate_number, plate_number_enc FROM telegram_users WHERE chat_id = :chatId"));
  query.bindValue(":chatId", chatId);

  if (query.exec() && query.next()) {
    return resolveStoredPlate(query);
  }
  return QString();
}

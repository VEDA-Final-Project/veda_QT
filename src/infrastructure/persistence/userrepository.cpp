#include "infrastructure/persistence/userrepository.h"
#include "infrastructure/persistence/databasecontext.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

UserRepository::UserRepository() {}

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
                     "  plate_number TEXT NOT NULL,"
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
      "ALTER TABLE telegram_users ADD COLUMN name TEXT",
      "ALTER TABLE telegram_users ADD COLUMN phone TEXT",
      "ALTER TABLE telegram_users ADD COLUMN payment_info TEXT"};

  for (const QString &alterSql : newColumns) {
    QSqlQuery alterQuery(db);
    alterQuery.exec(alterSql);
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
                     "plate_number, name, phone, payment_info, created_at) "
                     "VALUES (:chat_id, :plate, :name, :phone, :payment, "
                     "datetime('now','localtime'))"));
  query.bindValue(":chat_id", chatId);
  query.bindValue(":plate", plateNumber);
  query.bindValue(":name", name);
  query.bindValue(":phone", phone);
  query.bindValue(":payment", paymentInfo);

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
      QStringLiteral("UPDATE telegram_users SET plate_number=:plate, "
                     "name=:name, phone=:phone, payment_info=:payment "
                     "WHERE chat_id=:chat_id"));
  query.bindValue(":chat_id", chatId);
  query.bindValue(":plate", plateNumber);
  query.bindValue(":name", name);
  query.bindValue(":phone", phone);
  query.bindValue(":payment", paymentInfo);
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
  if (!query.exec(
          QStringLiteral("SELECT chat_id, plate_number FROM telegram_users"))) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return users;
  }

  while (query.next()) {
    users.insert(query.value(0).toString(), query.value(1).toString());
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
  if (!query.exec(QStringLiteral("SELECT chat_id, plate_number, name, phone, "
                                 "payment_info, created_at FROM telegram_users "
                                 "ORDER BY created_at DESC"))) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["chat_id"] = query.value("chat_id").toString();
    row["plate_number"] = query.value("plate_number").toString();
    row["name"] = query.value("name").toString();
    row["phone"] = query.value("phone").toString();
    row["payment_info"] = query.value("payment_info").toString();
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
  query.bindValue(":plate", plateNumber);

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
      "SELECT plate_number FROM telegram_users WHERE chat_id = :chatId"));
  query.bindValue(":chatId", chatId);

  if (query.exec() && query.next()) {
    return query.value(0).toString();
  }
  return QString();
}

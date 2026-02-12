#include "database/userrepository.h"
#include "database/databasecontext.h"
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
  return true;
}

bool UserRepository::registerUser(const QString &chatId,
                                  const QString &plateNumber,
                                  QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  query.prepare(
      QStringLiteral("INSERT OR REPLACE INTO telegram_users (chat_id, "
                     "plate_number, created_at) "
                     "VALUES (:chat_id, :plate, datetime('now','localtime'))"));
  query.bindValue(":chat_id", chatId);
  query.bindValue(":plate", plateNumber);

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

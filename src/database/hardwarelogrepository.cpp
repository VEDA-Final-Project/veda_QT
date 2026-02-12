#include "database/hardwarelogrepository.h"
#include "database/databasecontext.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>


HardwareLogRepository::HardwareLogRepository() {}

bool HardwareLogRepository::init(QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = "Database is not open";
    return false;
  }

  QSqlQuery query(db);
  const QString sql =
      QStringLiteral("CREATE TABLE IF NOT EXISTS hardware_logs ("
                     "  log_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "  zone_id TEXT,"
                     "  device_type TEXT NOT NULL,"
                     "  action TEXT NOT NULL,"
                     "  timestamp TEXT DEFAULT (datetime('now','localtime'))"
                     ")");

  if (!query.exec(sql)) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return false;
  }
  return true;
}

bool HardwareLogRepository::addLog(const QString &zoneId,
                                   const QString &deviceType,
                                   const QString &action,
                                   QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen())
    return false;

  QSqlQuery query(db);
  query.prepare(
      "INSERT INTO hardware_logs (zone_id, device_type, action, timestamp) "
      "VALUES (:zone, :device, :action, datetime('now','localtime'))");
  query.bindValue(":zone", zoneId);
  query.bindValue(":device", deviceType);
  query.bindValue(":action", action);

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    qWarning() << "[HardwareLog] Insert failed:" << query.lastError().text();
    return false;
  }
  return true;
}

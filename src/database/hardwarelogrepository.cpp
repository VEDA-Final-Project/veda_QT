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

QList<QJsonObject>
HardwareLogRepository::getAllLogs(QString *errorMessage) const {
  QList<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();

  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = "Database is not open";
    return results;
  }

  QSqlQuery query(db);
  if (!query.exec("SELECT log_id, zone_id, device_type, action, timestamp FROM "
                  "hardware_logs ORDER BY timestamp DESC")) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["log_id"] = query.value("log_id").toInt();
    row["zone_id"] = query.value("zone_id").toString();
    row["device_type"] = query.value("device_type").toString();
    row["action"] = query.value("action").toString();
    row["timestamp"] = query.value("timestamp").toString();
    results.append(row);
  }
  return results;
}

bool HardwareLogRepository::deleteLog(int logId, QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();

  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = "Database is not open";
    return false;
  }

  QSqlQuery query(db);
  query.prepare("DELETE FROM hardware_logs WHERE log_id = :id");
  query.bindValue(":id", logId);

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return false;
  }
  return true;
}

bool HardwareLogRepository::clearLogs(QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();

  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = "Database is not open";
    return false;
  }

  QSqlQuery query(db);
  if (!query.exec("DELETE FROM hardware_logs")) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return false;
  }
  return true;
}

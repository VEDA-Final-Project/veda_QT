#include "database/mediarepository.h"
#include "database/databasecontext.h"
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

MediaRepository::MediaRepository() {}

bool MediaRepository::init(QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage)
      *errorMessage = QStringLiteral("Database is not open");
    return false;
  }

  QSqlQuery query(db);
  const QString sql =
      QStringLiteral("CREATE TABLE IF NOT EXISTS media_logs ("
                     "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "  type TEXT NOT NULL,"
                     "  description TEXT,"
                     "  camera_id TEXT,"
                     "  file_path TEXT NOT NULL,"
                     "  created_at TEXT DEFAULT (datetime('now','localtime'))"
                     ")");

  if (!query.exec(sql)) {
    const QString err = QStringLiteral("Failed to create media_logs table: ") +
                        query.lastError().text();
    qWarning() << err;
    if (errorMessage)
      *errorMessage = err;
    return false;
  }

  return true;
}

bool MediaRepository::addMediaRecord(const QString &type,
                                     const QString &description,
                                     const QString &cameraId,
                                     const QString &filePath,
                                     QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "INSERT INTO media_logs (type, description, camera_id, file_path, "
      "created_at) "
      "VALUES (:type, :desc, :cam, :path, :created_at)"));
  query.bindValue(":type", type);
  query.bindValue(":desc", description);
  query.bindValue(":cam", cameraId);
  query.bindValue(":path", filePath);
  query.bindValue(":created_at",
                  QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));

  if (!query.exec()) {
    const QString err = QStringLiteral("Failed to add media record: ") +
                        query.lastError().text();
    qWarning() << err;
    if (errorMessage)
      *errorMessage = err;
    return false;
  }
  return true;
}

QVector<QJsonObject>
MediaRepository::getAllMediaRecords(QString *errorMessage) const {
  QVector<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);

  if (!query.exec(QStringLiteral(
          "SELECT id, type, description, camera_id, created_at, file_path "
          "FROM media_logs ORDER BY created_at DESC"))) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["id"] = query.value("id").toInt();
    row["type"] = query.value("type").toString();
    row["description"] = query.value("description").toString();
    row["camera_id"] = query.value("camera_id").toString();
    row["created_at"] = query.value("created_at").toString();
    row["file_path"] = query.value("file_path").toString();
    results.append(row);
  }
  return results;
}

QVector<QJsonObject>
MediaRepository::getMediaRecordsByCamera(const QString &cameraId,
                                         QString *errorMessage) const {
  QVector<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);

  query.prepare(QStringLiteral(
      "SELECT id, type, description, camera_id, created_at, file_path "
      "FROM media_logs WHERE camera_id = :cam ORDER BY created_at DESC"));
  query.bindValue(":cam", cameraId);

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["id"] = query.value("id").toInt();
    row["type"] = query.value("type").toString();
    row["description"] = query.value("description").toString();
    row["camera_id"] = query.value("camera_id").toString();
    row["created_at"] = query.value("created_at").toString();
    row["file_path"] = query.value("file_path").toString();
    results.append(row);
  }
  return results;
}

QVector<QJsonObject> MediaRepository::getMediaRecordsByTypeAndCamera(
    const QString &type, const QString &cameraId, QString *errorMessage) const {
  QVector<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);

  query.prepare(QStringLiteral(
      "SELECT id, type, description, camera_id, created_at, file_path "
      "FROM media_logs WHERE type = :type AND camera_id = :cam ORDER BY "
      "created_at ASC"));
  query.bindValue(":type", type);
  query.bindValue(":cam", cameraId);

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["id"] = query.value("id").toInt();
    row["type"] = query.value("type").toString();
    row["description"] = query.value("description").toString();
    row["camera_id"] = query.value("camera_id").toString();
    row["created_at"] = query.value("created_at").toString();
    row["file_path"] = query.value("file_path").toString();
    results.append(row);
  }
  return results;
}

bool MediaRepository::deleteMediaRecord(int id, QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);
  query.prepare(QStringLiteral("DELETE FROM media_logs WHERE id = :id"));
  query.bindValue(":id", id);

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return false;
  }
  return true;
}

QVector<QJsonObject>
MediaRepository::getOldMediaRecords(int hours, QString *errorMessage) const {
  return getOldMediaRecordsByMinutes(hours * 60, errorMessage);
}

QVector<QJsonObject>
MediaRepository::getOldMediaRecordsByMinutes(int minutes,
                                             QString *errorMessage) const {
  QVector<QJsonObject> results;
  QSqlDatabase db = DatabaseContext::database();
  QSqlQuery query(db);

  query.prepare(QStringLiteral("SELECT id, type, file_path FROM media_logs "
                               "WHERE created_at < :cutoff"));
  QString cutoff = QDateTime::currentDateTime()
                       .addSecs(-minutes * 60)
                       .toString("yyyy-MM-dd HH:mm:ss");
  query.bindValue(":cutoff", cutoff);

  if (!query.exec()) {
    if (errorMessage)
      *errorMessage = query.lastError().text();
    return results;
  }

  while (query.next()) {
    QJsonObject row;
    row["id"] = query.value("id").toInt();
    row["type"] = query.value("type").toString();
    row["file_path"] = query.value("file_path").toString();
    results.append(row);
  }
  return results;
}

bool MediaRepository::cleanupMissingRecords(QString *errorMessage) {
  QVector<QJsonObject> allRecords = getAllMediaRecords(errorMessage);
  bool success = true;

  for (const auto &record : allRecords) {
    int id = record["id"].toInt();
    QString filePath = record["file_path"].toString();

    if (!QFile::exists(filePath)) {
      qDebug() << "[MediaRepo] Removing orphaned record. ID:" << id
               << "Path:" << filePath;
      if (!deleteMediaRecord(id, errorMessage)) {
        success = false;
      }
    }
  }
  return success;
}

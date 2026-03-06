#include "roi/roirepository.h"
#include "database/databasecontext.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {
const QString kDefaultCameraKey = QStringLiteral("camera");

QString normalizedCameraKey(const QString &cameraKey) {
  const QString trimmed = cameraKey.trimmed();
  return trimmed.isEmpty() ? kDefaultCameraKey : trimmed;
}

QString sqlError(const QSqlQuery &query, const QSqlDatabase &db) {
  const QString queryErr = query.lastError().text();
  if (!queryErr.isEmpty()) {
    return queryErr;
  }
  return db.lastError().text();
}

std::optional<QString> execSql(QSqlDatabase &db, const QString &sql) {
  QSqlQuery query(db);
  if (!query.exec(sql)) {
    return sqlError(query, db);
  }
  return std::nullopt;
}

Result<bool> tableHasColumn(QSqlDatabase &db, const QString &tableName,
                            const QString &columnName) {
  QSqlQuery query(db);
  if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
    return {false, sqlError(query, db)};
  }

  while (query.next()) {
    if (query.value(1).toString() == columnName) {
      return {true, QString()};
    }
  }
  return {false, QString()};
}

QString roiTableDdl() {
  return QStringLiteral("CREATE TABLE IF NOT EXISTS roi ("
                        "zone_id TEXT PRIMARY KEY,"
                        "zone_name TEXT NOT NULL,"
                        "camera_key TEXT NOT NULL DEFAULT 'camera',"
                        "zone_enable INTEGER NOT NULL DEFAULT 1,"
                        "zone_points TEXT NOT NULL,"
                        "bbox TEXT NOT NULL,"
                        "created_at TEXT NOT NULL"
                        ")");
}

Result<bool> tableUsesLegacyGlobalNameUnique(QSqlDatabase &db) {
  QSqlQuery listQuery(db);
  if (!listQuery.exec(QStringLiteral("PRAGMA index_list(roi)"))) {
    return {false, sqlError(listQuery, db)};
  }

  while (listQuery.next()) {
    const bool isUnique = listQuery.value(2).toInt() != 0;
    if (!isUnique) {
      continue;
    }

    const QString indexName = listQuery.value(1).toString();
    if (indexName.isEmpty()) {
      continue;
    }

    QSqlQuery indexInfoQuery(db);
    if (!indexInfoQuery.exec(
            QStringLiteral("PRAGMA index_info(%1)").arg(indexName))) {
      return {false, sqlError(indexInfoQuery, db)};
    }

    QStringList columns;
    while (indexInfoQuery.next()) {
      columns.append(indexInfoQuery.value(2).toString());
    }
    if (columns.size() == 1 && columns.first() == QStringLiteral("zone_name")) {
      return {true, QString()};
    }
  }
  return {false, QString()};
}

std::optional<QString> rebuildRoiTable(QSqlDatabase &db,
                                       const QString &copySelectSql) {
  if (auto err = execSql(db, QStringLiteral("BEGIN IMMEDIATE TRANSACTION"));
      err) {
    return err;
  }

  auto rollbackOnFailure = [&db]() { execSql(db, QStringLiteral("ROLLBACK")); };

  if (auto err = execSql(db, QStringLiteral("DROP TABLE IF EXISTS roi_new"));
      err) {
    rollbackOnFailure();
    return err;
  }

  if (auto err = execSql(
          db, QStringLiteral("CREATE TABLE roi_new ("
                             "zone_id TEXT PRIMARY KEY,"
                             "zone_name TEXT NOT NULL,"
                             "camera_key TEXT NOT NULL DEFAULT 'camera',"
                             "zone_enable INTEGER NOT NULL DEFAULT 1,"
                             "zone_points TEXT NOT NULL,"
                             "bbox TEXT NOT NULL,"
                             "created_at TEXT NOT NULL"
                             ")"));
      err) {
    rollbackOnFailure();
    return err;
  }

  const QString copySql = QStringLiteral("INSERT INTO roi_new "
                                         "(zone_id, zone_name, camera_key, "
                                         "zone_enable, zone_points, bbox, "
                                         "created_at) ") +
                          copySelectSql;
  if (auto err = execSql(db, copySql); err) {
    rollbackOnFailure();
    return err;
  }

  if (auto err = execSql(db, QStringLiteral("DROP TABLE roi")); err) {
    rollbackOnFailure();
    return err;
  }

  if (auto err =
          execSql(db, QStringLiteral("ALTER TABLE roi_new RENAME TO roi"));
      err) {
    rollbackOnFailure();
    return err;
  }

  if (auto err = execSql(db, QStringLiteral("COMMIT")); err) {
    rollbackOnFailure();
    return err;
  }
  return std::nullopt;
}
} // namespace

RoiRepository::RoiRepository() {}

RoiRepository::~RoiRepository() {}

std::optional<QString> RoiRepository::init() { return ensureSchema(); }

Result<QVector<QJsonObject>> RoiRepository::loadAll() const {
  return loadByCameraKey(QString());
}

Result<QVector<QJsonObject>>
RoiRepository::loadByCameraKey(const QString &cameraKey) const {
  QVector<QJsonObject> records;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return {records, QStringLiteral("DB 연결이 열려있지 않습니다.")};
  }

  QSqlQuery query(db);
  const QString normalizedKey = normalizedCameraKey(cameraKey);
  if (cameraKey.trimmed().isEmpty()) {
    query.prepare(QStringLiteral(
        "SELECT zone_id, zone_name, camera_key, zone_enable, zone_points, "
        "bbox, created_at "
        "FROM roi ORDER BY datetime(created_at) ASC, zone_id ASC"));
  } else {
    query.prepare(QStringLiteral(
        "SELECT zone_id, zone_name, camera_key, zone_enable, zone_points, "
        "bbox, created_at "
        "FROM roi WHERE camera_key = ? "
        "ORDER BY datetime(created_at) ASC, zone_id ASC"));
    query.addBindValue(normalizedKey);
  }

  if (!query.exec()) {
    return {records, sqlError(query, db)};
  }

  while (query.next()) {
    QJsonParseError pointsErr;
    QJsonParseError bboxErr;
    const QJsonArray points =
        QJsonDocument::fromJson(query.value(4).toByteArray(), &pointsErr)
            .array();
    const QJsonObject bbox =
        QJsonDocument::fromJson(query.value(5).toByteArray(), &bboxErr)
            .object();
    if (pointsErr.error != QJsonParseError::NoError ||
        bboxErr.error != QJsonParseError::NoError) {
      continue;
    }

    QJsonObject record{
        {"zone_id", query.value(0).toString()},
        {"zone_name", query.value(1).toString()},
        {"camera_key", normalizedCameraKey(query.value(2).toString())},
        {"zone_enable", query.value(3).toInt() != 0},
        {"zone_points", points},
        {"bbox", bbox},
        {"created_at", query.value(6).toString()},
    };
    if (isValidRoiRecord(record)) {
      records.append(record);
    }
  }
  return {records, QString()};
}

std::optional<QString> RoiRepository::upsert(const QJsonObject &roiData) {
  if (!isValidRoiRecord(roiData)) {
    return QStringLiteral("유효하지 않은 ROI 레코드입니다.");
  }
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return QStringLiteral("DB 연결이 열려있지 않습니다.");
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral("INSERT INTO roi "
                               "(zone_id, zone_name, camera_key, zone_enable, "
                               "zone_points, bbox, created_at) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?) "
                               "ON CONFLICT(zone_id) DO UPDATE SET "
                               "zone_name = excluded.zone_name, "
                               "camera_key = excluded.camera_key, "
                               "zone_enable = excluded.zone_enable, "
                               "zone_points = excluded.zone_points, "
                               "bbox = excluded.bbox"));

  query.addBindValue(roiData.value("zone_id").toString());
  query.addBindValue(roiData.value("zone_name").toString());
  query.addBindValue(
      normalizedCameraKey(roiData.value("camera_key").toString()));
  query.addBindValue(roiData.value("zone_enable").toBool() ? 1 : 0);
  query.addBindValue(QJsonDocument(roiData.value("zone_points").toArray())
                         .toJson(QJsonDocument::Compact));
  query.addBindValue(QJsonDocument(roiData.value("bbox").toObject())
                         .toJson(QJsonDocument::Compact));
  query.addBindValue(roiData.value("created_at").toString());

  if (!query.exec()) {
    return sqlError(query, db);
  }
  return std::nullopt;
}

std::optional<QString> RoiRepository::removeById(const QString &zoneId) {
  if (zoneId.isEmpty()) {
    return QStringLiteral("삭제할 zone_id가 비어 있습니다.");
  }
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return QStringLiteral("DB 연결이 열려있지 않습니다.");
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral("DELETE FROM roi WHERE zone_id = ?"));
  query.addBindValue(zoneId);
  if (!query.exec()) {
    return sqlError(query, db);
  }
  return std::nullopt;
}

std::optional<QString> RoiRepository::ensureSchema() {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    return QStringLiteral("DB 연결이 열려있지 않습니다.");
  }

  QString err;
  if (auto e = execSql(db, roiTableDdl()); e) {
    return e;
  }

  Result<bool> hasCameraKey =
      tableHasColumn(db, QStringLiteral("roi"), QStringLiteral("camera_key"));
  if (!hasCameraKey.isOk()) {
    return hasCameraKey.error;
  }
  if (!hasCameraKey.data) {
    if (auto e =
            execSql(db, QStringLiteral("ALTER TABLE roi ADD COLUMN camera_key "
                                       "TEXT NOT NULL DEFAULT 'camera'"));
        e) {
      return e;
    }
  }

  Result<bool> hasLegacyGlobalNameUnique = tableUsesLegacyGlobalNameUnique(db);
  if (!hasLegacyGlobalNameUnique.isOk()) {
    return hasLegacyGlobalNameUnique.error;
  }
  if (hasLegacyGlobalNameUnique.data) {
    if (auto e = rebuildRoiTable(
            db,
            QStringLiteral("SELECT zone_id, zone_name, "
                           "COALESCE(NULLIF(TRIM(camera_key), ''), 'camera'), "
                           "COALESCE(zone_enable, 1), zone_points, bbox, "
                           "created_at "
                           "FROM roi"));
        e) {
      return e;
    }
  }

  if (auto e = execSql(
          db, QStringLiteral("DROP INDEX IF EXISTS idx_roi_name_unique"));
      e) {
    return e;
  }

  if (auto e = execSql(
          db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS "
                             "idx_roi_camera_name_unique "
                             "ON roi(camera_key, zone_name COLLATE NOCASE)"));
      e) {
    return e;
  }

  return std::nullopt;
}

bool RoiRepository::isValidRoiRecord(const QJsonObject &roiData) {
  return !roiData.value("zone_id").toString().isEmpty() &&
         !roiData.value("zone_name").toString().isEmpty() &&
         !roiData.value("camera_key").toString().isEmpty() &&
         roiData.value("zone_points").isArray() &&
         roiData.value("bbox").isObject() &&
         !roiData.value("created_at").toString().isEmpty();
}

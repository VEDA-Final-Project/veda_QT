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

bool execSql(QSqlDatabase &db, const QString &sql, QString *errorMessage) {
  QSqlQuery query(db);
  if (!query.exec(sql)) {
    if (errorMessage) {
      *errorMessage = sqlError(query, db);
    }
    return false;
  }
  return true;
}

bool tableHasColumn(QSqlDatabase &db, const QString &tableName,
                    const QString &columnName, bool *hasColumn,
                    QString *errorMessage) {
  if (!hasColumn) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("컬럼 검사 결과 포인터가 null입니다.");
    }
    return false;
  }

  *hasColumn = false;
  QSqlQuery query(db);
  if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
    if (errorMessage) {
      *errorMessage = sqlError(query, db);
    }
    return false;
  }

  while (query.next()) {
    if (query.value(1).toString() == columnName) {
      *hasColumn = true;
      break;
    }
  }
  return true;
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

bool tableUsesLegacyGlobalNameUnique(QSqlDatabase &db, bool *isLegacy,
                                     QString *errorMessage) {
  if (!isLegacy) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("legacy 결과 포인터가 null입니다.");
    }
    return false;
  }
  *isLegacy = false;

  QSqlQuery listQuery(db);
  if (!listQuery.exec(QStringLiteral("PRAGMA index_list(roi)"))) {
    if (errorMessage) {
      *errorMessage = sqlError(listQuery, db);
    }
    return false;
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
      if (errorMessage) {
        *errorMessage = sqlError(indexInfoQuery, db);
      }
      return false;
    }

    QStringList columns;
    while (indexInfoQuery.next()) {
      columns.append(indexInfoQuery.value(2).toString());
    }
    if (columns.size() == 1 &&
        (columns.first() == QStringLiteral("rod_name") ||
         columns.first() == QStringLiteral("zone_name"))) {
      *isLegacy = true;
      return true;
    }
  }
  return true;
}

bool rebuildRoiTable(QSqlDatabase &db, const QString &copySelectSql,
                     QString *errorMessage) {
  if (!execSql(db, QStringLiteral("BEGIN IMMEDIATE TRANSACTION"), errorMessage)) {
    return false;
  }

  auto rollbackOnFailure = [&db, &errorMessage]() {
    execSql(db, QStringLiteral("ROLLBACK"), errorMessage);
  };

  if (!execSql(db, QStringLiteral("DROP TABLE IF EXISTS roi_new"),
               errorMessage)) {
    rollbackOnFailure();
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE TABLE roi_new ("
                              "zone_id TEXT PRIMARY KEY,"
                              "zone_name TEXT NOT NULL,"
                              "camera_key TEXT NOT NULL DEFAULT 'camera',"
                              "zone_enable INTEGER NOT NULL DEFAULT 1,"
                              "zone_points TEXT NOT NULL,"
                              "bbox TEXT NOT NULL,"
                              "created_at TEXT NOT NULL"
                              ")"),
               errorMessage)) {
    rollbackOnFailure();
    return false;
  }

  const QString copySql = QStringLiteral("INSERT INTO roi_new "
                                         "(zone_id, zone_name, camera_key, "
                                         "zone_enable, zone_points, bbox, "
                                         "created_at) ") +
                          copySelectSql;
  if (!execSql(db, copySql, errorMessage)) {
    rollbackOnFailure();
    return false;
  }

  if (!execSql(db, QStringLiteral("DROP TABLE roi"), errorMessage)) {
    rollbackOnFailure();
    return false;
  }

  if (!execSql(db, QStringLiteral("ALTER TABLE roi_new RENAME TO roi"),
               errorMessage)) {
    rollbackOnFailure();
    return false;
  }

  if (!execSql(db, QStringLiteral("COMMIT"), errorMessage)) {
    rollbackOnFailure();
    return false;
  }
  return true;
}
} // namespace

RoiRepository::RoiRepository() {}

RoiRepository::~RoiRepository() {}

bool RoiRepository::init(QString *errorMessage) {
  return ensureSchema(errorMessage);
}

QVector<QJsonObject> RoiRepository::loadAll(QString *errorMessage) const {
  return loadByCameraKey(QString(), errorMessage);
}

QVector<QJsonObject>
RoiRepository::loadByCameraKey(const QString &cameraKey,
                               QString *errorMessage) const {
  QVector<QJsonObject> records;
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("DB 연결이 열려있지 않습니다.");
    }
    return records;
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
    if (errorMessage) {
      *errorMessage = sqlError(query, db);
    }
    return records;
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
  return records;
}

bool RoiRepository::upsert(const QJsonObject &roiData, QString *errorMessage) {
  if (!isValidRoiRecord(roiData)) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("유효하지 않은 ROI 레코드입니다.");
    }
    return false;
  }
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("DB 연결이 열려있지 않습니다.");
    }
    return false;
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
  query.addBindValue(normalizedCameraKey(roiData.value("camera_key").toString()));
  query.addBindValue(roiData.value("zone_enable").toBool() ? 1 : 0);
  query.addBindValue(QJsonDocument(roiData.value("zone_points").toArray())
                         .toJson(QJsonDocument::Compact));
  query.addBindValue(QJsonDocument(roiData.value("bbox").toObject())
                         .toJson(QJsonDocument::Compact));
  query.addBindValue(roiData.value("created_at").toString());

  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = sqlError(query, db);
    }
    return false;
  }
  return true;
}

bool RoiRepository::removeById(const QString &zoneId, QString *errorMessage) {
  if (zoneId.isEmpty()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("삭제할 zone_id가 비어 있습니다.");
    }
    return false;
  }
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("DB 연결이 열려있지 않습니다.");
    }
    return false;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral("DELETE FROM roi WHERE zone_id = ?"));
  query.addBindValue(zoneId);
  if (!query.exec()) {
    if (errorMessage) {
      *errorMessage = sqlError(query, db);
    }
    return false;
  }
  return true;
}

bool RoiRepository::ensureSchema(QString *errorMessage) {
  QSqlDatabase db = DatabaseContext::database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("DB 연결이 열려있지 않습니다.");
    }
    return false;
  }

  if (!execSql(db, roiTableDdl(), errorMessage)) {
    return false;
  }

  bool hasCameraKey = false;
  if (!tableHasColumn(db, QStringLiteral("roi"), QStringLiteral("camera_key"),
                      &hasCameraKey, errorMessage)) {
    return false;
  }
  if (!hasCameraKey) {
    if (!execSql(db, QStringLiteral("ALTER TABLE roi ADD COLUMN camera_key "
                                    "TEXT NOT NULL DEFAULT 'camera'"),
                 errorMessage)) {
      return false;
    }
  }

  bool hasRodId = false;
  if (!tableHasColumn(db, QStringLiteral("roi"), QStringLiteral("rod_id"),
                      &hasRodId, errorMessage)) {
    return false;
  }
  if (hasRodId) {
    if (!rebuildRoiTable(db,
                         QStringLiteral(
                             "SELECT "
                             "COALESCE(NULLIF(TRIM(rod_id), ''), "
                             "'zone-' || lower(hex(randomblob(8)))), "
                             "rod_name, "
                             "COALESCE(NULLIF(TRIM(camera_key), ''), 'camera'), "
                             "COALESCE(rod_enable, 1), "
                             "rod_points, bbox, created_at "
                             "FROM roi"),
                         errorMessage)) {
      return false;
    }
  }

  bool hasLegacyGlobalNameUnique = false;
  if (!tableUsesLegacyGlobalNameUnique(db, &hasLegacyGlobalNameUnique,
                                       errorMessage)) {
    return false;
  }
  if (hasLegacyGlobalNameUnique &&
      !rebuildRoiTable(db,
                       QStringLiteral(
                           "SELECT zone_id, zone_name, "
                           "COALESCE(NULLIF(TRIM(camera_key), ''), 'camera'), "
                           "COALESCE(zone_enable, 1), zone_points, bbox, "
                           "created_at "
                           "FROM roi"),
                       errorMessage)) {
    return false;
  }

  if (!execSql(db, QStringLiteral("DROP INDEX IF EXISTS idx_roi_name_unique"),
               errorMessage)) {
    return false;
  }

  if (!execSql(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS "
                                  "idx_roi_camera_name_unique "
                                  "ON roi(camera_key, zone_name COLLATE NOCASE)"),
               errorMessage)) {
    return false;
  }

  return true;
}

bool RoiRepository::isValidRoiRecord(const QJsonObject &roiData) {
  return !roiData.value("zone_id").toString().isEmpty() &&
         !roiData.value("zone_name").toString().isEmpty() &&
         !roiData.value("camera_key").toString().isEmpty() &&
         roiData.value("zone_points").isArray() &&
         roiData.value("bbox").isObject() &&
         !roiData.value("created_at").toString().isEmpty();
}

#include "roi/roirepository.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace
{
  QString sqlError(const QSqlQuery &query, const QSqlDatabase &db)
  {
    const QString queryErr = query.lastError().text();
    if (!queryErr.isEmpty())
    {
      return queryErr;
    }
    return db.lastError().text();
  }

  bool execSql(QSqlDatabase &db, const QString &sql, QString *errorMessage)
  {
    QSqlQuery query(db);
    if (!query.exec(sql))
    {
      if (errorMessage)
      {
        *errorMessage = sqlError(query, db);
      }
      return false;
    }
    return true;
  }

  bool tableHasColumn(QSqlDatabase &db, const QString &tableName,
                      const QString &columnName, bool *hasColumn,
                      QString *errorMessage)
  {
    if (!hasColumn)
    {
      if (errorMessage)
      {
        *errorMessage = QStringLiteral("컬럼 검사 결과 포인터가 null입니다.");
      }
      return false;
    }

    *hasColumn = false;
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName)))
    {
      if (errorMessage)
      {
        *errorMessage = sqlError(query, db);
      }
      return false;
    }

    while (query.next())
    {
      if (query.value(1).toString() == columnName)
      {
        *hasColumn = true;
        break;
      }
    }
    return true;
  }

  QString roiTableDdl()
  {
    return QStringLiteral(
        "CREATE TABLE IF NOT EXISTS roi ("
        "rod_id TEXT PRIMARY KEY,"
        "rod_name TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        "rod_enable INTEGER NOT NULL DEFAULT 1,"
        "rod_purpose TEXT NOT NULL,"
        "rod_points TEXT NOT NULL,"
        "bbox TEXT NOT NULL,"
        "created_at TEXT NOT NULL"
        ")");
  }
} // namespace

RoiRepository::RoiRepository()
    : m_connectionName(QString("roi_repo_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

RoiRepository::~RoiRepository()
{
  if (QSqlDatabase::contains(m_connectionName))
  {
    {
      QSqlDatabase db = QSqlDatabase::database(m_connectionName);
      if (db.isOpen())
      {
        db.close();
      }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
  }
}

bool RoiRepository::init(const QString &dbFilePath, QString *errorMessage)
{
  if (dbFilePath.isEmpty())
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("DB 경로가 비어 있습니다.");
    }
    return false;
  }

  const QFileInfo fileInfo(dbFilePath);
  QDir dir;
  if (!dir.mkpath(fileInfo.absolutePath()))
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("DB 디렉토리 생성 실패: %1").arg(fileInfo.absolutePath());
    }
    return false;
  }

  QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  db.setDatabaseName(dbFilePath);
  if (!db.open())
  {
    if (errorMessage)
    {
      *errorMessage = db.lastError().text();
    }
    return false;
  }
  return ensureSchema(errorMessage);
}

QVector<QJsonObject> RoiRepository::loadAll(QString *errorMessage) const
{
  QVector<QJsonObject> records;
  if (!QSqlDatabase::contains(m_connectionName))
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("ROI DB 연결이 초기화되지 않았습니다.");
    }
    return records;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery query(db);
  if (!query.exec(QStringLiteral(
          "SELECT rod_id, rod_name, rod_enable, rod_purpose, rod_points, bbox, created_at "
          "FROM roi ORDER BY datetime(created_at) ASC, rod_id ASC")))
  {
    if (errorMessage)
    {
      *errorMessage = sqlError(query, db);
    }
    return records;
  }

  while (query.next())
  {
    QJsonParseError pointsErr;
    QJsonParseError bboxErr;
    const QJsonArray points =
        QJsonDocument::fromJson(query.value(4).toByteArray(), &pointsErr).array();
    const QJsonObject bbox =
        QJsonDocument::fromJson(query.value(5).toByteArray(), &bboxErr).object();
    if (pointsErr.error != QJsonParseError::NoError || bboxErr.error != QJsonParseError::NoError)
    {
      continue;
    }

    QJsonObject record{
        {"rod_id", query.value(0).toString()},
        {"rod_name", query.value(1).toString()},
        {"rod_enable", query.value(2).toInt() != 0},
        {"rod_purpose", query.value(3).toString()},
        {"rod_points", points},
        {"bbox", bbox},
        {"created_at", query.value(6).toString()},
    };
    if (isValidRoiRecord(record))
    {
      records.append(record);
    }
  }
  return records;
}

bool RoiRepository::upsert(const QJsonObject &roiData, QString *errorMessage)
{
  if (!isValidRoiRecord(roiData))
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("유효하지 않은 ROI 레코드입니다.");
    }
    return false;
  }
  if (!QSqlDatabase::contains(m_connectionName))
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("ROI DB 연결이 초기화되지 않았습니다.");
    }
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "INSERT INTO roi (rod_id, rod_name, rod_enable, rod_purpose, rod_points, bbox, created_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(rod_id) DO UPDATE SET "
      "rod_name = excluded.rod_name, "
      "rod_enable = excluded.rod_enable, "
      "rod_purpose = excluded.rod_purpose, "
      "rod_points = excluded.rod_points, "
      "bbox = excluded.bbox"));

  query.addBindValue(roiData.value("rod_id").toString());
  query.addBindValue(roiData.value("rod_name").toString());
  query.addBindValue(roiData.value("rod_enable").toBool() ? 1 : 0);
  query.addBindValue(roiData.value("rod_purpose").toString());
  query.addBindValue(QJsonDocument(roiData.value("rod_points").toArray())
                         .toJson(QJsonDocument::Compact));
  query.addBindValue(
      QJsonDocument(roiData.value("bbox").toObject()).toJson(QJsonDocument::Compact));
  query.addBindValue(roiData.value("created_at").toString());

  if (!query.exec())
  {
    if (errorMessage)
    {
      *errorMessage = sqlError(query, db);
    }
    return false;
  }
  return true;
}

bool RoiRepository::removeById(const QString &rodId, QString *errorMessage)
{
  if (rodId.isEmpty())
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("삭제할 rod_id가 비어 있습니다.");
    }
    return false;
  }
  if (!QSqlDatabase::contains(m_connectionName))
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("ROI DB 연결이 초기화되지 않았습니다.");
    }
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery query(db);
  query.prepare(QStringLiteral("DELETE FROM roi WHERE rod_id = ?"));
  query.addBindValue(rodId);
  if (!query.exec())
  {
    if (errorMessage)
    {
      *errorMessage = sqlError(query, db);
    }
    return false;
  }
  return true;
}

bool RoiRepository::ensureSchema(QString *errorMessage)
{
  if (!QSqlDatabase::contains(m_connectionName))
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("ROI DB 연결이 초기화되지 않았습니다.");
    }
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!execSql(db, roiTableDdl(), errorMessage))
  {
    return false;
  }

  bool hasUpdatedAt = false;
  if (!tableHasColumn(db, QStringLiteral("roi"), QStringLiteral("updated_at"),
                      &hasUpdatedAt, errorMessage))
  {
    return false;
  }
  if (!hasUpdatedAt)
  {
    return true;
  }

  if (!db.transaction())
  {
    if (errorMessage)
    {
      *errorMessage = db.lastError().text();
    }
    return false;
  }

  if (!execSql(db, QStringLiteral("ALTER TABLE roi RENAME TO roi_old"),
               errorMessage) ||
      !execSql(db, roiTableDdl(), errorMessage) ||
      !execSql(db,
               QStringLiteral(
                   "INSERT INTO roi (rod_id, rod_name, rod_enable, rod_purpose, "
                   "rod_points, bbox, created_at) "
                   "SELECT rod_id, rod_name, rod_enable, rod_purpose, rod_points, "
                   "bbox, created_at FROM roi_old"),
               errorMessage) ||
      !execSql(db, QStringLiteral("DROP TABLE roi_old"), errorMessage))
  {
    db.rollback();
    return false;
  }

  if (!db.commit())
  {
    if (errorMessage)
    {
      *errorMessage = db.lastError().text();
    }
    return false;
  }
  return true;
}

bool RoiRepository::isValidRoiRecord(const QJsonObject &roiData)
{
  return !roiData.value("rod_id").toString().isEmpty() &&
         !roiData.value("rod_name").toString().isEmpty() &&
         !roiData.value("rod_purpose").toString().isEmpty() &&
         roiData.value("rod_points").isArray() &&
         roiData.value("bbox").isObject() &&
         !roiData.value("created_at").toString().isEmpty();
}

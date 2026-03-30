#include "infrastructure/persistence/databasecontext.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

const QString DatabaseContext::ConnectionName =
    QStringLiteral("VEDA_DB_CONNECTION");

namespace {
constexpr int kTargetSchemaVersion = 2;

QString quotedSqlString(QString value) {
  value.replace(QStringLiteral("'"), QStringLiteral("''"));
  return QStringLiteral("'%1'").arg(value);
}

bool ensureParentDirectoryExists(const QString &filePath,
                                 QString *errorMessage) {
  const QFileInfo fileInfo(filePath);
  QDir parentDir = fileInfo.dir();
  if (parentDir.exists()) {
    return true;
  }

  if (parentDir.mkpath(QStringLiteral("."))) {
    return true;
  }

  if (errorMessage) {
    *errorMessage =
        QStringLiteral("Failed to create backup directory: %1")
            .arg(parentDir.absolutePath());
  }
  return false;
}

bool replaceWithCopiedFile(const QString &sourcePath, const QString &targetPath,
                           QString *errorMessage) {
  if (QFile::exists(targetPath) && !QFile::remove(targetPath)) {
    if (errorMessage) {
      *errorMessage =
          QStringLiteral("Failed to overwrite backup file: %1").arg(targetPath);
    }
    return false;
  }

  if (QFile::copy(sourcePath, targetPath)) {
    return true;
  }

  if (errorMessage) {
    *errorMessage = QStringLiteral("Failed to copy DB file from %1 to %2")
                        .arg(sourcePath, targetPath);
  }
  return false;
}

bool execSchemaSql(QSqlDatabase &db, const QString &sql, QString *errorMessage) {
  QSqlQuery query(db);
  if (query.exec(sql)) {
    return true;
  }

  if (errorMessage) {
    *errorMessage = query.lastError().text();
  }
  return false;
}

bool resetSchemaIfNeeded(QSqlDatabase &db, QString *errorMessage) {
  QSqlQuery versionQuery(db);
  if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) ||
      !versionQuery.next()) {
    if (errorMessage) {
      *errorMessage = versionQuery.lastError().text();
    }
    return false;
  }

  if (versionQuery.value(0).toInt() == kTargetSchemaVersion) {
    return true;
  }

  if (!execSchemaSql(db, QStringLiteral("BEGIN IMMEDIATE TRANSACTION"),
                     errorMessage)) {
    return false;
  }

  const QStringList dropStatements = {
      QStringLiteral("DROP TABLE IF EXISTS media_logs"),
      QStringLiteral("DROP TABLE IF EXISTS parking_logs"),
      QStringLiteral("DROP TABLE IF EXISTS user_vehicles"),
      QStringLiteral("DROP TABLE IF EXISTS vehicle_plates"),
      QStringLiteral("DROP TABLE IF EXISTS telegram_users"),
      QStringLiteral("DROP TABLE IF EXISTS vehicles"),
      QStringLiteral("DROP TABLE IF EXISTS roi")};

  for (const QString &sql : dropStatements) {
    if (!execSchemaSql(db, sql, errorMessage)) {
      execSchemaSql(db, QStringLiteral("ROLLBACK"), nullptr);
      return false;
    }
  }

  if (!execSchemaSql(
          db,
          QStringLiteral("PRAGMA user_version = %1")
              .arg(kTargetSchemaVersion),
          errorMessage)) {
    execSchemaSql(db, QStringLiteral("ROLLBACK"), nullptr);
    return false;
  }

  if (!execSchemaSql(db, QStringLiteral("COMMIT"), errorMessage)) {
    execSchemaSql(db, QStringLiteral("ROLLBACK"), nullptr);
    return false;
  }

  qDebug() << "[DB] Reset schema to version" << kTargetSchemaVersion;
  return true;
}
} // namespace

bool DatabaseContext::init(const QString &dbPath, QString *errorMessage) {
  if (QSqlDatabase::contains(ConnectionName)) {
    return true; // 이미 초기화됨
  }

  QSqlDatabase db =
      QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), ConnectionName);
  db.setDatabaseName(dbPath);

  if (!db.open()) {
    const QString err =
        QStringLiteral("Failed to open main DB: ") + db.lastError().text();
    qWarning() << err;
    if (errorMessage) {
      *errorMessage = err;
    }
    return false;
  }

  // 외래 키 제약 조건 활성화 및 WAL 모드 설정 (성능/안전성)
  QSqlQuery query(db);
  query.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
  query.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
  query.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));

  if (!resetSchemaIfNeeded(db, errorMessage)) {
    return false;
  }

  qDebug() << "[DB] Initialized database at:" << dbPath;
  return true;
}

QSqlDatabase DatabaseContext::database() {
  return QSqlDatabase::database(ConnectionName);
}

QString DatabaseContext::databasePath() {
  const QSqlDatabase db = database();
  if (!db.isValid()) {
    return QString();
  }
  return db.databaseName();
}

bool DatabaseContext::backupDatabase(const QString &backupPath,
                                     QString *errorMessage) {
  const QSqlDatabase db = database();
  if (!db.isOpen()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Database is not open");
    }
    return false;
  }

  const QString trimmedBackupPath = backupPath.trimmed();
  if (trimmedBackupPath.isEmpty()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Backup path is empty");
    }
    return false;
  }

  const QString normalizedBackupPath =
      QFileInfo(trimmedBackupPath).absoluteFilePath();
  const QString normalizedSourcePath = QFileInfo(databasePath()).absoluteFilePath();
  if (!normalizedSourcePath.isEmpty() &&
      normalizedBackupPath == normalizedSourcePath) {
    if (errorMessage) {
      *errorMessage =
          QStringLiteral("Backup path must be different from the source DB");
    }
    return false;
  }

  if (!ensureParentDirectoryExists(normalizedBackupPath, errorMessage)) {
    return false;
  }

  QSqlQuery backupQuery(db);
  const QString vacuumSql = QStringLiteral("VACUUM INTO %1")
                                .arg(quotedSqlString(normalizedBackupPath));
  if (backupQuery.exec(vacuumSql)) {
    qDebug() << "[DB] Backup created via VACUUM INTO:" << normalizedBackupPath;
    return true;
  }

  const QString vacuumError = backupQuery.lastError().text();
  qWarning() << "[DB] VACUUM INTO backup failed, fallback to checkpoint+copy:"
             << vacuumError;

  QSqlQuery checkpointQuery(db);
  if (!checkpointQuery.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"))) {
    const QString err =
        QStringLiteral("Backup failed: %1")
            .arg(checkpointQuery.lastError().text());
    if (errorMessage) {
      *errorMessage = err;
    }
    return false;
  }

  if (normalizedSourcePath.isEmpty() || !QFileInfo::exists(normalizedSourcePath)) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Source DB file does not exist");
    }
    return false;
  }

  if (!replaceWithCopiedFile(normalizedSourcePath, normalizedBackupPath,
                             errorMessage)) {
    return false;
  }

  qDebug() << "[DB] Backup created via file copy:" << normalizedBackupPath;
  return true;
}

bool DatabaseContext::createTimestampedBackup(const QString &backupDirPath,
                                              QString *createdBackupPath,
                                              QString *errorMessage) {
  const QString sourcePath = databasePath();
  const QFileInfo sourceInfo(sourcePath);
  const QString baseName =
      sourceInfo.completeBaseName().isEmpty() ? QStringLiteral("veda")
                                              : sourceInfo.completeBaseName();
  const QString suffix =
      sourceInfo.suffix().isEmpty() ? QStringLiteral("db")
                                    : sourceInfo.suffix();
  const QString timestamp =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
  const QString fileName =
      QStringLiteral("%1_%2.%3").arg(baseName, timestamp, suffix);
  const QString backupPath = QDir(backupDirPath).filePath(fileName);

  if (!backupDatabase(backupPath, errorMessage)) {
    return false;
  }

  if (createdBackupPath) {
    *createdBackupPath = QFileInfo(backupPath).absoluteFilePath();
  }
  return true;
}

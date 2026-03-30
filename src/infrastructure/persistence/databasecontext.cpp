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

#include "infrastructure/persistence/databasecontext.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

const QString DatabaseContext::ConnectionName =
    QStringLiteral("VEDA_DB_CONNECTION");

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

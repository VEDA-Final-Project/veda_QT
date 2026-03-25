#ifndef METADATATHREAD_H
#define METADATATHREAD_H

#include "infrastructure/metadata/objectinfo.h"

#include <QMutex>
#include <QSet>
#include <QThread>

class MetadataThread : public QThread {
  Q_OBJECT
public:
  explicit MetadataThread(QObject *parent = nullptr);
  ~MetadataThread();

  void setConnectionInfo(const QString &ip, const QString &user,
                         const QString &password, const QString &profile);
  void setDisabledTypes(const QSet<QString> &types);
  void stop();

signals:
  void metadataReceived(const QList<ObjectInfo> &objects);
  void logMessage(const QString &msg);

protected:
  void run() override;

private:
  QString m_ip;
  QString m_user;
  QString m_password;
  QString m_profile;

  QMutex m_mutex;
  QSet<QString> m_disabledTypes;
};

#endif // METADATATHREAD_H

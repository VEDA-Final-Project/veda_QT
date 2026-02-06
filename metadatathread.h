
#ifndef METADATATHREAD_H
#define METADATATHREAD_H

#include <QMutex>
#include <QProcess>
#include <QRect>
#include <QThread>


struct ObjectInfo {
  int id;
  QString type;      // Person, Vehicle, Face...
  QString extraInfo; // License Plate Number, etc.
  QRect rect;        // 0~1000 Normalized Coordinate or Pixel Coordinate
};

class MetadataThread : public QThread {
  Q_OBJECT
public:
  explicit MetadataThread(QObject *parent = nullptr);
  ~MetadataThread();

  void setConnectionInfo(const QString &ip, const QString &user,
                         const QString &password);
  void stop();

signals:
  void metadataReceived(const QList<ObjectInfo> &objects);
  void logMessage(const QString &msg);

protected:
  void run() override;

private slots:
  void onReadyReadStandardOutput();

private:
  void processBuffer();
  void parseFrame(const QString &frameXml);

private:
  QString m_ip;
  QString m_user;
  QString m_password;

  QProcess *m_process;
  QMutex m_mutex;
  QByteArray m_buffer;
};

#endif // METADATATHREAD_H

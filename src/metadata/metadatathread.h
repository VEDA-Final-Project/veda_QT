#ifndef METADATATHREAD_H
#define METADATATHREAD_H

#include <QMutex>
#include <QProcess>
#include <QRectF>
#include <QSet>
#include <QThread>
#include <vector>

struct ObjectInfo {
  int id;
  QString type;      // Person, Vehicle, Face...
  QString extraInfo; // License Plate Number, etc. (Legacy)
  QString plate;     // Explicit plate number
  float score;       // Confidence score
  QRectF rect;       // 0~1000 Normalized Coordinate or Pixel Coordinate
  std::vector<float> reidFeatures;
  QString reidId;
};

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

private slots:
  void onReadyReadStandardOutput();

private:
  void processBuffer();
  void parseFrame(const QString &frameXml);
  QString findFFmpegPath();

private:
  QString m_ip;
  QString m_user;
  QString m_password;
  QString m_profile;

  QProcess *m_process;
  QMutex m_mutex;
  QByteArray m_buffer;
  QSet<QString> m_disabledTypes;
};

#endif // METADATATHREAD_H

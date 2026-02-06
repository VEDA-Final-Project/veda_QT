
#ifndef METADATATHREAD_H
#define METADATATHREAD_H

#include <QThread>
#include <QProcess>
#include <QMutex>
#include <QRect>

struct ObjectInfo {
    int id;
    QString type; // Person, Vehicle, Face...
    QString extraInfo; // License Plate Number, etc.
    QRect rect;   // 0~1000 Normalized Coordinate or Pixel Coordinate
};

class MetadataThread : public QThread
{
    Q_OBJECT
public:
    explicit MetadataThread(QObject *parent = nullptr);
    ~MetadataThread();

    void setConnectionInfo(const QString &ip, const QString &user, const QString &password);
    void stop();

signals:
    void metadataReceived(const QList<ObjectInfo> &objects);
    void logMessage(const QString &msg);

protected:
    void run() override;

private slots:
    void onReadyReadStandardOutput();

private:
    QString m_ip;
    QString m_user;
    QString m_password;
    bool m_stop;

    QProcess *m_process;
    QMutex m_mutex;
    QByteArray m_buffer;
};

#endif // METADATATHREAD_H

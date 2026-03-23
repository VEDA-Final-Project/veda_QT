#ifndef SRTPRTSPCLIENT_H
#define SRTPRTSPCLIENT_H

#include <QObject>
#include <QSslSocket>
#include <QString>
#include <QMap>
#include <QList>
#include <QPair>

struct RtspRequestInfo {
  QString method;
  QString url;
};

using RtspHeaderList = QList<QPair<QString, QString>>;

class SrtpRtspClient : public QObject {
  Q_OBJECT
public:
  explicit SrtpRtspClient(QObject *parent = nullptr);
  
  void setSocket(QSslSocket *socket);
  void setCredentials(const QString &user, const QString &pass);
  
  void sendOptions(const QString &url);
  void sendDescribe(const QString &url, const QByteArray &mikeyData = QByteArray());
  void sendSetup(const QString &trackUrl,
                 const QString &transport,
                 const QByteArray &mikeyData = QByteArray(),
                 bool isMetadata = false,
                 const QString &sessionId = QString(),
                 const QString &metadataRequire = QString());
  void sendPlay(const QString &url, const QString &sessionId);
  void sendGetParameter(const QString &url, const QString &sessionId);
  void sendTeardown(const QString &url, const QString &sessionId);

signals:
  void interleavedDataReceived(quint8 channel, const QByteArray &data);
  void responseReceived(int cseq,
                        int statusCode,
                        const QString &statusText,
                        const QMap<QString, QString> &headers,
                        const QByteArray &body,
                        const QString &requestMethod,
                        const QString &requestUrl,
                        bool matchedRequest);
  void logMessage(const QString &msg);

private slots:
  void onReadyRead();

private:
  void sendRequest(const QString &method,
                   const QString &url,
                   const RtspHeaderList &headers = RtspHeaderList(),
                   const QByteArray &body = QByteArray());
  void parseResponse(const QByteArray &data);
  void resetState();

  QSslSocket *m_socket;
  int m_cseq;
  QByteArray m_buffer;
  QString m_user;
  QString m_password;
  QString m_nonce;
  QString m_realm;
  QMap<int, RtspRequestInfo> m_pendingRequests;
};

class AuthHelper {
public:
  bool isValid() const { return !m_user.isEmpty(); }
  void setCredentials(const QString &u, const QString &p) { m_user = u; m_password = p; }
  void setChallenge(const QString &r, const QString &n) { m_realm = r; m_nonce = n; }
  
  QString getAuthHeader(const QString &method, const QString &url);

private:
  QString m_user, m_password, m_realm, m_nonce;
};

#endif // SRTPRTSPCLIENT_H

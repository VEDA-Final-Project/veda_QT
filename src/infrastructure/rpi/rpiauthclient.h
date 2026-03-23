#ifndef RPIAUTHCLIENT_H
#define RPIAUTHCLIENT_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>

class QSslError;
class QSslSocket;
class QTimer;

class RpiAuthClient : public QObject {
  Q_OBJECT

public:
  explicit RpiAuthClient(QObject *parent = nullptr);

  void setTimeouts(int connectTimeoutMs, int requestTimeoutMs);

  bool beginLogin(const QString &host, quint16 port, const QString &username,
                  const QString &password);
  bool submitTotp(const QString &otp);
  void cancel();

  bool isBusy() const;
  bool isWaitingForTotp() const;

signals:
  void loginStepFinished(bool ok, const QString &code, const QString &message);
  void authFinished(bool ok, const QString &code, const QString &message);

private slots:
  void onConnected();
  void onEncrypted();
  void onReadyRead();
  void onSocketErrorOccurred();
  void onSslErrors(const QList<QSslError> &errors);
  void onConnectTimeout();
  void onRequestTimeout();

private:
  enum class SessionState {
    Idle,
    Connecting,
    WaitingReady,
    WaitingLoginResult,
    WaitingForTotp,
    WaitingTotpResult,
    Authenticated
  };

  void emitLoginStepResult(bool ok, const QString &code,
                           const QString &message);
  void emitAuthResult(bool ok, const QString &code, const QString &message);
  void resetSession();
  void finishConnected();
  bool sendLoginRequest();
  bool sendTotpRequest(const QString &otp);
  bool sendMessage(const QByteArray &json);
  void processIncomingBuffer();
  void handleMessage(const QByteArray &payload);
  bool authTlsEnabled() const;
  QStringList configuredPinnedFingerprints() const;
  bool shouldAllowPinnedCertificate() const;
  static QString normalizeFingerprint(const QString &value);

  QSslSocket *m_socket = nullptr;
  QTimer *m_connectTimer = nullptr;
  QTimer *m_requestTimer = nullptr;
  QByteArray m_readBuffer;
  QString m_host;
  quint16 m_port = 0;
  QString m_username;
  QString m_password;
  QString m_preAuthToken;
  SessionState m_state = SessionState::Idle;
  qint32 m_expectedPayloadLength = -1;
  int m_connectTimeoutMs = 3000;
  int m_requestTimeoutMs = 5000;
  qint64 m_preAuthExpiresAt = 0;
  bool m_requestInFlight = false;
};

#endif // RPIAUTHCLIENT_H

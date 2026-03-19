#ifndef RPIAUTHCLIENT_H
#define RPIAUTHCLIENT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtGlobal>

class QTcpSocket;
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
  void onReadyRead();
  void onSocketErrorOccurred();
  void onConnectTimeout();
  void onRequestTimeout();

private:
  enum class SessionState {
    Idle,
    Connecting,
    WaitingReady,
    WaitingLoginResult,
    WaitingForTotp,
    WaitingTotpResult
  };

  void emitLoginStepResult(bool ok, const QString &code,
                           const QString &message);
  void emitAuthResult(bool ok, const QString &code, const QString &message);
  void resetSession();
  bool sendLoginRequest();
  bool sendTotpRequest(const QString &otp);
  bool sendMessage(const QByteArray &json);
  void processIncomingBuffer();
  void handleMessage(const QByteArray &payload);

  QTcpSocket *m_socket = nullptr;
  QTimer *m_connectTimer = nullptr;
  QTimer *m_requestTimer = nullptr;
  QByteArray m_readBuffer;
  QString m_username;
  QString m_password;
  SessionState m_state = SessionState::Idle;
  qint32 m_expectedPayloadLength = -1;
  int m_connectTimeoutMs = 3000;
  int m_requestTimeoutMs = 5000;
  bool m_requestInFlight = false;
};

#endif // RPIAUTHCLIENT_H

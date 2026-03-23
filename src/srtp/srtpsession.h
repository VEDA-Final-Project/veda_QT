#ifndef SRTPSESSION_H
#define SRTPSESSION_H

#include <QObject>
#include <QStringList>
#include <QSslSocket>
#include <QSslError>
#include <QList>

/**
 * @brief SRTP 세션 관리 클래스 (Step 1)
 * - 카메라 322 포트로 TLS 보안 터널(QSslSocket)을 연결합니다.
 */
class SrtpSession : public QObject {
  Q_OBJECT
public:
  explicit SrtpSession(QObject *parent = nullptr);
  virtual ~SrtpSession();

  void connectToCamera(const QString &ip, int port = 322);
  void disconnectFromCamera();

  bool isEncrypted() const;
  QSslSocket* socket() const { return m_sslSocket; }
  QString errorString() const;

signals:
  void encrypted();      // TLS 핸드셰이크 성공 시 발생
  void disconnected();    // 연결 종료 시 발생
  void error(const QString &msg); // 오류 발생 시 상세 메시지 전달

private slots:
  void onEncrypted();
  void onSslErrors(const QList<QSslError> &errors);
  void onErrorOccurred(QAbstractSocket::SocketError socketError);
  void onDisconnected();

private:
  bool isPinnedCertificateAllowed() const;
  QString peerCertificateSha256() const;

  QSslSocket *m_sslSocket;
  QString m_targetIp;
  QStringList m_allowedFingerprints;
};

#endif // SRTPSESSION_H

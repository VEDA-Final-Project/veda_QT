#include "srtpsession.h"
#include <QSslConfiguration>
#include <QSslCipher>
#include <QDebug>

SrtpSession::SrtpSession(QObject *parent)
    : QObject(parent), m_sslSocket(new QSslSocket(this)) {
    
  connect(m_sslSocket, &QSslSocket::encrypted, this, &SrtpSession::onEncrypted);
  connect(m_sslSocket, &QSslSocket::sslErrors, this, &SrtpSession::onSslErrors);
  connect(m_sslSocket, &QSslSocket::errorOccurred, this, &SrtpSession::onErrorOccurred);
  connect(m_sslSocket, &QSslSocket::disconnected, this, &SrtpSession::onDisconnected);
}

SrtpSession::~SrtpSession() {
  disconnectFromCamera();
}

void SrtpSession::connectToCamera(const QString &ip, int port) {
  m_targetIp = ip;

  QSslConfiguration sslConfig = m_sslSocket->sslConfiguration();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  sslConfig.setProtocol(QSsl::TlsV1_3OrLater);
#else
  sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
#endif
  m_sslSocket->setSslConfiguration(sslConfig);

  m_sslSocket->connectToHostEncrypted(ip, port);
}

void SrtpSession::disconnectFromCamera() {
  if (m_sslSocket->isOpen()) {
    m_sslSocket->disconnectFromHost();
  }
}

bool SrtpSession::isEncrypted() const {
  return m_sslSocket->isEncrypted();
}

QString SrtpSession::errorString() const {
  return m_sslSocket->errorString();
}

void SrtpSession::onEncrypted() {
  emit encrypted();
}

void SrtpSession::onSslErrors(const QList<QSslError> &errors) {
  Q_UNUSED(errors);
  m_sslSocket->ignoreSslErrors();
}

void SrtpSession::onErrorOccurred(QAbstractSocket::SocketError socketError) {
  qWarning() << "[SRTP][Step1] Socket Error:" << socketError << "-" << m_sslSocket->errorString();
  emit error(m_sslSocket->errorString());
}

void SrtpSession::onDisconnected() {
  emit disconnected();
}

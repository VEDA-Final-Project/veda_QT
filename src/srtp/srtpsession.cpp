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
  qDebug() << "[SRTP][Step1] Connecting to TLS tunnel:" << ip << ":" << port;

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
  qDebug() << "[SRTP][Step1] TLS Handshake Success! Tunnel encrypted."
           << "protocol:" << m_sslSocket->sessionProtocol()
           << "cipher:" << m_sslSocket->sessionCipher().name();
  emit encrypted();
}

void SrtpSession::onSslErrors(const QList<QSslError> &errors) {
  for (const QSslError &error : errors) {
    qDebug() << "[SRTP][Step1] SSL Error (Ignoring):" << error.errorString();
  }
  m_sslSocket->ignoreSslErrors();
}

void SrtpSession::onErrorOccurred(QAbstractSocket::SocketError socketError) {
  qWarning() << "[SRTP][Step1] Socket Error:" << socketError << "-" << m_sslSocket->errorString();
  emit error(m_sslSocket->errorString());
}

void SrtpSession::onDisconnected() {
  qDebug() << "[SRTP][Step1] TLS Tunnel disconnected.";
  emit disconnected();
}

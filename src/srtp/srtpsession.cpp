#include "srtpsession.h"
#include <QCryptographicHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QSslCipher>
#include <QDebug>

namespace {
QString normalizeFingerprint(QString value) {
  value.remove(':');
  value.remove(' ');
  return value.trimmed().toLower();
}

QStringList loadPinnedFingerprints() {
  const QString envValue = QProcessEnvironment::systemEnvironment()
                               .value(QStringLiteral("VEDA_TLS_PINNED_SHA256"))
                               .trimmed();
  if (!envValue.isEmpty()) {
    QStringList values = envValue.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                        Qt::SkipEmptyParts);
    for (QString &value : values) {
      value = normalizeFingerprint(value);
    }
    values.removeAll(QString());
    values.removeDuplicates();
    return values;
  }

  return QStringList{
      QStringLiteral(
          "c14cfa33c9a713649c52dab20e5de0b75ba2a62522f3a42e6341a3b08572df13"),
      QStringLiteral(
          "f7fa3ac68fbdde39662a5595954e8bc9fdfa7af5bb1b84a71feae4ab4b718fbb")};
}
} // namespace

SrtpSession::SrtpSession(QObject *parent)
    : QObject(parent),
      m_sslSocket(new QSslSocket(this)),
      m_allowedFingerprints(loadPinnedFingerprints()) {
    
  connect(m_sslSocket, &QSslSocket::encrypted, this, &SrtpSession::onEncrypted);
  connect(m_sslSocket, &QSslSocket::sslErrors, this, &SrtpSession::onSslErrors);
  connect(m_sslSocket, &QSslSocket::errorOccurred, this, &SrtpSession::onErrorOccurred);
  connect(m_sslSocket, &QSslSocket::disconnected, this, &SrtpSession::onDisconnected);
}

SrtpSession::~SrtpSession() {
  disconnectFromCamera();
}

void SrtpSession::setAllowedFingerprints(const QStringList &fingerprints) {
  if (fingerprints.isEmpty()) {
    m_allowedFingerprints = loadPinnedFingerprints();
    return;
  }

  QStringList normalized;
  for (QString fingerprint : fingerprints) {
    fingerprint = normalizeFingerprint(std::move(fingerprint));
    if (!fingerprint.isEmpty()) {
      normalized.append(fingerprint);
    }
  }
  normalized.removeDuplicates();
  m_allowedFingerprints = normalized;
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
  if (!isPinnedCertificateAllowed()) {
    qWarning() << "[SRTP][TLS] Encrypted connection rejected by certificate pinning. peerSha256:"
               << peerCertificateSha256();
    emit error(QStringLiteral("TLS certificate pinning check failed."));
    m_sslSocket->disconnectFromHost();
    return;
  }
  emit encrypted();
}

void SrtpSession::onSslErrors(const QList<QSslError> &errors) {
  if (isPinnedCertificateAllowed()) {
    m_sslSocket->ignoreSslErrors(errors);
    return;
  }

  qWarning() << "[SRTP][TLS] Certificate pinning check failed. peerSha256:"
             << peerCertificateSha256();
  emit error(QStringLiteral("TLS certificate pinning check failed."));
  m_sslSocket->disconnectFromHost();
}

void SrtpSession::onErrorOccurred(QAbstractSocket::SocketError socketError) {
  qWarning() << "[SRTP][Step1] Socket Error:" << socketError << "-" << m_sslSocket->errorString();
  emit error(m_sslSocket->errorString());
}

void SrtpSession::onDisconnected() {
  emit disconnected();
}

QString SrtpSession::peerCertificateSha256() const {
  const QSslCertificate cert = m_sslSocket->peerCertificate();
  if (cert.isNull()) {
    return QString();
  }
  return QString::fromLatin1(
      cert.digest(QCryptographicHash::Sha256).toHex()).toLower();
}

bool SrtpSession::isPinnedCertificateAllowed() const {
  if (m_allowedFingerprints.isEmpty()) {
    return false;
  }

  const QString peerSha256 = normalizeFingerprint(peerCertificateSha256());
  if (!peerSha256.isEmpty() && m_allowedFingerprints.contains(peerSha256)) {
    return true;
  }

  const auto chain = m_sslSocket->peerCertificateChain();
  for (const QSslCertificate &cert : chain) {
    const QString sha256 = normalizeFingerprint(
        QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex()));
    if (!sha256.isEmpty() && m_allowedFingerprints.contains(sha256)) {
      return true;
    }
  }

  return false;
}

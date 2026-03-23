#include "rpiauthclient.h"

#include "config/config.h"

#include <QAbstractSocket>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslCertificate>
#include <QSslError>
#include <QSslSocket>
#include <QTimer>
#include <QtEndian>
#include <QtGlobal>

namespace {
constexpr quint32 kMaxPayloadBytes = 4096;

QString normalizedServerCode(const QJsonObject &message,
                             const QString &fallback) {
  const QString code = message.value(QStringLiteral("code")).toString().trimmed();
  return code.isEmpty() ? fallback : code;
}
} // namespace

RpiAuthClient::RpiAuthClient(QObject *parent) : QObject(parent) {
  m_socket = new QSslSocket(this);
  m_connectTimer = new QTimer(this);
  m_requestTimer = new QTimer(this);

  m_connectTimer->setSingleShot(true);
  m_requestTimer->setSingleShot(true);

  connect(m_socket, &QSslSocket::connected, this, &RpiAuthClient::onConnected);
  connect(m_socket, &QSslSocket::encrypted, this, &RpiAuthClient::onEncrypted);
  connect(m_socket, &QSslSocket::readyRead, this, &RpiAuthClient::onReadyRead);
  connect(m_socket, &QSslSocket::errorOccurred, this,
          &RpiAuthClient::onSocketErrorOccurred);
  connect(m_socket, &QSslSocket::sslErrors, this, &RpiAuthClient::onSslErrors);
  connect(m_connectTimer, &QTimer::timeout, this,
          &RpiAuthClient::onConnectTimeout);
  connect(m_requestTimer, &QTimer::timeout, this,
          &RpiAuthClient::onRequestTimeout);
}

void RpiAuthClient::setTimeouts(int connectTimeoutMs, int requestTimeoutMs) {
  m_connectTimeoutMs = qMax(1, connectTimeoutMs);
  m_requestTimeoutMs = qMax(1, requestTimeoutMs);
}

bool RpiAuthClient::beginLogin(const QString &host, quint16 port,
                               const QString &username,
                               const QString &password) {
  if (m_state != SessionState::Idle) {
    return false;
  }

  m_readBuffer.clear();
  m_expectedPayloadLength = -1;
  m_host = host.trimmed();
  m_port = port;
  m_username = username;
  m_password = password;
  m_preAuthToken.clear();
  m_preAuthExpiresAt = 0;
  m_state = SessionState::Connecting;
  m_requestInFlight = true;

  m_socket->abort();
  if (authTlsEnabled()) {
    m_socket->connectToHostEncrypted(m_host, m_port);
  } else {
    m_socket->connectToHost(m_host, m_port);
  }
  m_connectTimer->start(m_connectTimeoutMs);
  return true;
}

bool RpiAuthClient::submitTotp(const QString &otp) {
  if (m_state != SessionState::WaitingForTotp || m_requestInFlight) {
    return false;
  }
  if (m_socket->state() != QAbstractSocket::ConnectedState) {
    resetSession();
    return false;
  }
  if (!m_preAuthExpiresAt ||
      QDateTime::currentSecsSinceEpoch() >= m_preAuthExpiresAt) {
    resetSession();
    return false;
  }
  if (!sendTotpRequest(otp)) {
    resetSession();
    return false;
  }

  m_state = SessionState::WaitingTotpResult;
  m_requestInFlight = true;
  m_requestTimer->start(m_requestTimeoutMs);
  return true;
}

void RpiAuthClient::cancel() { resetSession(); }

bool RpiAuthClient::isBusy() const { return m_requestInFlight; }

bool RpiAuthClient::isWaitingForTotp() const {
  return m_state == SessionState::WaitingForTotp;
}

void RpiAuthClient::onConnected() {
  if (m_state != SessionState::Connecting || authTlsEnabled()) {
    return;
  }
  finishConnected();
}

void RpiAuthClient::onEncrypted() {
  if (m_state != SessionState::Connecting || !authTlsEnabled()) {
    return;
  }
  finishConnected();
}

void RpiAuthClient::onReadyRead() {
  if (m_state == SessionState::Idle) {
    return;
  }

  m_readBuffer.append(m_socket->readAll());
  processIncomingBuffer();
}

void RpiAuthClient::onSocketErrorOccurred() {
  if (m_state == SessionState::Idle) {
    return;
  }

  const SessionState previousState = m_state;
  const QString message = m_socket->errorString();
  resetSession();

  if (previousState == SessionState::WaitingForTotp ||
      previousState == SessionState::WaitingTotpResult) {
    emitAuthResult(false, QStringLiteral("service_unavailable"), message);
    return;
  }
  emitLoginStepResult(false, QStringLiteral("service_unavailable"), message);
}

void RpiAuthClient::onSslErrors(const QList<QSslError> &errors) {
  if (!authTlsEnabled()) {
    return;
  }

  if (shouldAllowPinnedCertificate()) {
    m_socket->ignoreSslErrors(errors);
  }
}

void RpiAuthClient::onConnectTimeout() {
  if (m_state != SessionState::Connecting) {
    return;
  }

  resetSession();
  emitLoginStepResult(false, QStringLiteral("service_unavailable"),
                      QStringLiteral("Connection timeout"));
}

void RpiAuthClient::onRequestTimeout() {
  if (m_state == SessionState::WaitingForTotp || m_state == SessionState::Idle ||
      m_state == SessionState::Authenticated) {
    return;
  }

  const SessionState previousState = m_state;
  resetSession();

  if (previousState == SessionState::WaitingTotpResult) {
    emitAuthResult(false, QStringLiteral("service_unavailable"),
                   QStringLiteral("2차 인증 응답이 없습니다."));
    return;
  }
  emitLoginStepResult(false, QStringLiteral("service_unavailable"),
                      QStringLiteral("로그인 응답이 없습니다."));
}

void RpiAuthClient::emitLoginStepResult(bool ok, const QString &code,
                                        const QString &message) {
  emit loginStepFinished(ok, code, message);
}

void RpiAuthClient::emitAuthResult(bool ok, const QString &code,
                                   const QString &message) {
  emit authFinished(ok, code, message);
}

void RpiAuthClient::resetSession() {
  m_connectTimer->stop();
  m_requestTimer->stop();
  m_state = SessionState::Idle;
  m_requestInFlight = false;
  m_readBuffer.clear();
  m_expectedPayloadLength = -1;
  m_username.clear();
  m_password.clear();
  m_preAuthToken.clear();
  m_preAuthExpiresAt = 0;
  m_socket->abort();
}

void RpiAuthClient::finishConnected() {
  m_connectTimer->stop();
  m_state = SessionState::WaitingReady;
  m_requestInFlight = true;
  m_requestTimer->start(m_requestTimeoutMs);
}

bool RpiAuthClient::sendLoginRequest() {
  QJsonObject message;
  message.insert(QStringLiteral("type"), QStringLiteral("login"));
  message.insert(QStringLiteral("username"), m_username);
  message.insert(QStringLiteral("password"), m_password);
  const bool ok =
      sendMessage(QJsonDocument(message).toJson(QJsonDocument::Compact));
  m_password.clear();
  return ok;
}

bool RpiAuthClient::sendTotpRequest(const QString &otp) {
  QJsonObject message;
  message.insert(QStringLiteral("type"), QStringLiteral("verify_2fa"));
  message.insert(QStringLiteral("pre_auth_token"), m_preAuthToken);
  message.insert(QStringLiteral("code"), otp);
  return sendMessage(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

bool RpiAuthClient::sendMessage(const QByteArray &json) {
  if (json.isEmpty() || json.size() > static_cast<int>(kMaxPayloadBytes) ||
      m_socket->state() != QAbstractSocket::ConnectedState) {
    return false;
  }

  QByteArray packet(4, Qt::Uninitialized);
  qToBigEndian<quint32>(static_cast<quint32>(json.size()),
                        reinterpret_cast<uchar *>(packet.data()));
  packet.append(json);

  const qint64 written = m_socket->write(packet);
  return written == packet.size();
}

void RpiAuthClient::processIncomingBuffer() {
  while (true) {
    if (m_expectedPayloadLength < 0) {
      if (m_readBuffer.size() < 4) {
        return;
      }

      const quint32 payloadLength = qFromBigEndian<quint32>(
          reinterpret_cast<const uchar *>(m_readBuffer.constData()));
      m_readBuffer.remove(0, 4);
    if (payloadLength == 0 || payloadLength > kMaxPayloadBytes) {
      const SessionState previousState = m_state;
      resetSession();
      if (previousState == SessionState::WaitingForTotp ||
          previousState == SessionState::WaitingTotpResult ||
          previousState == SessionState::Authenticated) {
        emitAuthResult(false, QStringLiteral("protocol_error"),
                       QStringLiteral("잘못된 인증 응답 길이입니다."));
      } else {
        emitLoginStepResult(false, QStringLiteral("protocol_error"),
                            QStringLiteral("잘못된 로그인 응답 길이입니다."));
      }
      return;
    }
      m_expectedPayloadLength = static_cast<qint32>(payloadLength);
    }

    if (m_readBuffer.size() < m_expectedPayloadLength) {
      return;
    }

    const QByteArray payload = m_readBuffer.left(m_expectedPayloadLength);
    m_readBuffer.remove(0, m_expectedPayloadLength);
    m_expectedPayloadLength = -1;

    handleMessage(payload);
    if (m_state == SessionState::Idle) {
      return;
    }
  }
}

void RpiAuthClient::handleMessage(const QByteArray &payload) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    const SessionState previousState = m_state;
    resetSession();
    if (previousState == SessionState::WaitingForTotp ||
        previousState == SessionState::WaitingTotpResult ||
        previousState == SessionState::Authenticated) {
      emitAuthResult(false, QStringLiteral("protocol_error"),
                     QStringLiteral("인증 서버 JSON 응답이 올바르지 않습니다."));
    } else {
      emitLoginStepResult(false, QStringLiteral("protocol_error"),
                          QStringLiteral("로그인 서버 JSON 응답이 올바르지 않습니다."));
    }
    return;
  }

  const QJsonObject message = doc.object();
  const QString type = message.value(QStringLiteral("type")).toString();
  const QString serverMessage =
      message.value(QStringLiteral("message")).toString();

  if (m_state == SessionState::WaitingReady) {
    if (type != QStringLiteral("ready")) {
      resetSession();
      emitLoginStepResult(false, QStringLiteral("protocol_error"),
                          QStringLiteral("로그인 준비 응답이 올바르지 않습니다."));
      return;
    }

    if (!sendLoginRequest()) {
      resetSession();
      emitLoginStepResult(false, QStringLiteral("service_unavailable"),
                          QStringLiteral("로그인 요청 전송에 실패했습니다."));
      return;
    }

    m_state = SessionState::WaitingLoginResult;
    m_requestInFlight = true;
    m_requestTimer->start(m_requestTimeoutMs);
    return;
  }

  if (m_state == SessionState::WaitingLoginResult) {
    m_requestTimer->stop();
    m_requestInFlight = false;

    if (type == QStringLiteral("login_ok")) {
      const QString preAuthToken =
          message.value(QStringLiteral("pre_auth_token")).toString().trimmed();
      const qint64 preAuthExpiresAt =
          message.value(QStringLiteral("pre_auth_expires_at")).toInteger();
      if (preAuthToken.isEmpty() || preAuthExpiresAt <= 0) {
        resetSession();
        emitLoginStepResult(false, QStringLiteral("protocol_error"),
                            QStringLiteral("pre_auth_token 응답이 없습니다."));
        return;
      }

      m_preAuthToken = preAuthToken;
      m_preAuthExpiresAt = preAuthExpiresAt;
      m_state = SessionState::WaitingForTotp;
      emitLoginStepResult(true, QStringLiteral("login_ok"), serverMessage);
      return;
    }

    if (type == QStringLiteral("error")) {
      const QString code =
          normalizedServerCode(message, QStringLiteral("invalid_credentials"));
      resetSession();
      emitLoginStepResult(false, code, serverMessage);
      return;
    }

    resetSession();
    emitLoginStepResult(false, QStringLiteral("protocol_error"),
                        QStringLiteral("로그인 응답 타입이 올바르지 않습니다."));
    return;
  }

  if (m_state == SessionState::WaitingTotpResult) {
    m_requestTimer->stop();
    m_requestInFlight = false;

    if (type == QStringLiteral("auth_ok")) {
      m_preAuthToken.clear();
      m_preAuthExpiresAt = 0;
      m_state = SessionState::Authenticated;
      emitAuthResult(true, QStringLiteral("ok"), serverMessage);
      return;
    }

    if (type == QStringLiteral("error")) {
      const QString code =
          normalizedServerCode(message, QStringLiteral("invalid_otp"));
      m_state = SessionState::WaitingForTotp;
      emitAuthResult(false, code, serverMessage);
      return;
    }

    resetSession();
    emitAuthResult(false, QStringLiteral("protocol_error"),
                   QStringLiteral("2차 인증 응답 타입이 올바르지 않습니다."));
    return;
  }
}

bool RpiAuthClient::authTlsEnabled() const {
  return Config::instance().authTlsEnabled();
}

QStringList RpiAuthClient::configuredPinnedFingerprints() const {
  QStringList pins;
  const QString envPins =
      qEnvironmentVariable("VEDA_AUTH_TLS_PINNED_SHA256").trimmed();
  if (!envPins.isEmpty()) {
    const QString normalized = envPins;
    const QChar separators[] = {QChar(','), QChar(';')};
    QStringList parts;
    parts.append(normalized);
    for (const QChar separator : separators) {
      QStringList next;
      for (const QString &part : parts) {
        next.append(part.split(separator, Qt::SkipEmptyParts));
      }
      parts = next;
    }
    for (const QString &part : parts) {
      const QString value = normalizeFingerprint(part);
      if (!value.isEmpty()) {
        pins.append(value);
      }
    }
  }

  const QStringList configPins = Config::instance().authPinnedSha256();
  for (const QString &entry : configPins) {
    const QString value = normalizeFingerprint(entry);
    if (!value.isEmpty() && !pins.contains(value)) {
      pins.append(value);
    }
  }

  return pins;
}

bool RpiAuthClient::shouldAllowPinnedCertificate() const {
  const QStringList pins = configuredPinnedFingerprints();
  if (pins.isEmpty()) {
    return false;
  }

  const QSslCertificate cert = m_socket->peerCertificate();
  if (cert.isNull()) {
    return false;
  }

  const QString peerFingerprint =
      QString::fromLatin1(
          cert.digest(QCryptographicHash::Sha256).toHex()).toLower();
  return pins.contains(peerFingerprint);
}

QString RpiAuthClient::normalizeFingerprint(const QString &value) {
  QString normalized = value.trimmed().toLower();
  normalized.remove(QLatin1Char(':'));
  normalized.remove(QLatin1Char(' '));
  return normalized;
}

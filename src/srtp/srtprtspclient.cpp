#include "srtprtspclient.h"
#include <QDebug>
#include <QRegularExpression>
#include <QCryptographicHash>

namespace {
QString buildKeyMgmtHeaderValue(const QByteArray &mikeyData) {
  // Hanwha cameras expect this exact text shape in SETUP.
  return QString("prot=mikey; uri=\"\"; data=\"%1\"")
      .arg(QString::fromLatin1(mikeyData.toBase64()));
}
} // namespace

QString AuthHelper::getAuthHeader(const QString &method, const QString &url) {
  if (m_nonce.isEmpty()) {
    QString auth = QString("%1:%2").arg(m_user, m_password);
    return QString("Authorization: Basic %1").arg(QString::fromLatin1(auth.toUtf8().toBase64()));
  }
  QString a1 = QCryptographicHash::hash(QString("%1:%2:%3").arg(m_user, m_realm, m_password).toUtf8(), QCryptographicHash::Md5).toHex();
  QString a2 = QCryptographicHash::hash(QString("%1:%2").arg(method, url).toUtf8(), QCryptographicHash::Md5).toHex();
  QString response = QCryptographicHash::hash(QString("%1:%2:%3").arg(a1, m_nonce, a2).toUtf8(), QCryptographicHash::Md5).toHex();
  return QString("Authorization: Digest username=\"%1\", realm=\"%2\", nonce=\"%3\", uri=\"%4\", response=\"%5\"")
                 .arg(m_user, m_realm, m_nonce, url, response);
}

SrtpRtspClient::SrtpRtspClient(QObject *parent)
    : QObject(parent), m_socket(nullptr), m_cseq(1) {}

void SrtpRtspClient::setSocket(QSslSocket *socket) {
  m_socket = socket;
  if (m_socket) {
    resetState();
    connect(m_socket, &QSslSocket::readyRead, this, &SrtpRtspClient::onReadyRead);
    connect(m_socket, &QSslSocket::disconnected, this, &SrtpRtspClient::resetState);
  }
}

void SrtpRtspClient::setCredentials(const QString &user, const QString &pass) {
  m_user = user;
  m_password = pass;
}

void SrtpRtspClient::resetState() {
  m_cseq = 1;
  m_buffer.clear();
  m_nonce.clear();
  m_realm.clear();
  m_pendingRequests.clear();
}

void SrtpRtspClient::sendOptions(const QString &url) {
  sendRequest("OPTIONS", url);
}

void SrtpRtspClient::sendDescribe(const QString &url, const QByteArray &mikeyData) {
  RtspHeaderList headers;
  if (!mikeyData.isEmpty()) {
    headers.append(qMakePair(QStringLiteral("KeyMgmt"),
                             buildKeyMgmtHeaderValue(mikeyData)));
  }
  sendRequest("DESCRIBE", url, headers);
}

void SrtpRtspClient::sendSetup(const QString &trackUrl,
                              const QString &transport,
                              const QByteArray &mikeyData,
                              bool isMetadata,
                              const QString &sessionId,
                              const QString &metadataRequire) {
  RtspHeaderList headers;
  headers.append(qMakePair(QStringLiteral("Transport"), transport));
  if (!sessionId.isEmpty()) {
    headers.append(qMakePair(QStringLiteral("Session"), sessionId));
  }
  if (isMetadata && !metadataRequire.isEmpty()) {
    headers.append(qMakePair(QStringLiteral("Require"), metadataRequire));
  }
  if (!mikeyData.isEmpty()) {
    headers.append(qMakePair(QStringLiteral("KeyMgmt"),
                             buildKeyMgmtHeaderValue(mikeyData)));
  }
  sendRequest("SETUP", trackUrl, headers);
}

void SrtpRtspClient::sendPlay(const QString &url, const QString &sessionId) {
  RtspHeaderList headers;
  headers.append(qMakePair(QStringLiteral("Session"), sessionId));
  sendRequest("PLAY", url, headers);
}

void SrtpRtspClient::sendGetParameter(const QString &url,
                                      const QString &sessionId) {
  RtspHeaderList headers;
  headers.append(qMakePair(QStringLiteral("Session"), sessionId));
  headers.append(
      qMakePair(QStringLiteral("Content-Type"), QStringLiteral("text/parameters")));
  sendRequest("GET_PARAMETER", url, headers);
}

void SrtpRtspClient::sendTeardown(const QString &url, const QString &sessionId) {
  RtspHeaderList headers;
  headers.append(qMakePair(QStringLiteral("Session"), sessionId));
  sendRequest("TEARDOWN", url, headers);
}

void SrtpRtspClient::sendRequest(const QString &method,
                                 const QString &url,
                                 const RtspHeaderList &headers,
                                 const QByteArray &body) {
  if (!m_socket || !m_socket->isOpen()) return;

  const int cseq = m_cseq++;
  QString request = QString("%1 %2 RTSP/1.0\r\n").arg(method, url);
  request += QString("CSeq: %1\r\n").arg(cseq);
  request += "User-Agent: QtRTSP/1.0\r\n";

  if (!m_user.isEmpty()) {
    AuthHelper helper;
    helper.setCredentials(m_user, m_password);
    helper.setChallenge(m_realm, m_nonce);
    request += helper.getAuthHeader(method, url) + "\r\n";
  }

  for (const auto &header : headers) {
    request += QString("%1: %2\r\n").arg(header.first, header.second);
  }

  request += "\r\n";
  m_socket->write(request.toUtf8());
  if (!body.isEmpty()) m_socket->write(body);

  m_pendingRequests.insert(cseq, RtspRequestInfo{method, url});
  
}

void SrtpRtspClient::onReadyRead() {
  if (!m_socket) return;
  m_buffer.append(m_socket->readAll());
  
  while (true) {
    if (!m_buffer.isEmpty() && static_cast<quint8>(m_buffer[0]) == 0x24) {
      if (m_buffer.size() < 4) {
        break;
      }
      const quint8 channel = static_cast<quint8>(m_buffer[1]);
      const quint16 payloadLength =
          (static_cast<quint8>(m_buffer[2]) << 8) |
          static_cast<quint8>(m_buffer[3]);
      if (m_buffer.size() < 4 + payloadLength) {
        break;
      }
      const QByteArray payload = m_buffer.mid(4, payloadLength);
      m_buffer.remove(0, 4 + payloadLength);
      emit interleavedDataReceived(channel, payload);
      continue;
    }

    int headerEnd = m_buffer.indexOf("\r\n\r\n");
    if (headerEnd == -1) break;

    // Content-Length 확인을 위해 먼저 임시로 헤더를 들여다봅니다.
    QByteArray tempHeaders = m_buffer.left(headerEnd);
    int contentLength = 0;
    QStringList headerLines = QString::fromUtf8(tempHeaders).split("\r\n");
    for (const QString &line : headerLines) {
      if (line.toLower().startsWith("content-length:")) {
        contentLength = line.section(':', 1).trimmed().toInt();
        break;
      }
    }

    // 바디가 아직 덜 왔다면 파싱을 중단하고 더 기다립니다.
    if (contentLength > 0 && m_buffer.size() < (headerEnd + 4 + contentLength)) {
      break;
    }

    // 이제 완전히 수신된 것이 확실하므로 버퍼에서 실제로 제거하며 파싱합니다.
    m_buffer.remove(0, headerEnd + 4);

    QString rawResponse = QString::fromUtf8(tempHeaders);
    QStringList lines = rawResponse.split("\r\n");
    if (lines.isEmpty()) continue;

    QRegularExpression firstLineRe("RTSP/1\\.0 (\\d+) (.*)");
    QRegularExpressionMatch match = firstLineRe.match(lines[0]);
    if (!match.hasMatch()) continue;

    int statusCode = match.captured(1).toInt();
    QString statusText = match.captured(2);

    QMap<QString, QString> headers;
    int cseq = 0;

    for (int j = 1; j < lines.size(); ++j) {
      int colonPos = lines[j].indexOf(": ");
      if (colonPos != -1) {
        QString key = lines[j].left(colonPos).trimmed();
        QString value = lines[j].mid(colonPos + 2).trimmed();
        headers.insert(key, value);
        if (key.toLower() == "cseq") cseq = value.toInt();
        if (statusCode == 401 && key.toLower() == "www-authenticate") {
          QRegularExpression realmRe("realm=\"([^\"]+)\"");
          QRegularExpression nonceRe("nonce=\"([^\"]+)\"");
          QRegularExpressionMatch realmMatch = realmRe.match(value);
          QRegularExpressionMatch nonceMatch = nonceRe.match(value);
          if (realmMatch.hasMatch()) m_realm = realmMatch.captured(1);
          if (nonceMatch.hasMatch()) m_nonce = nonceMatch.captured(1);
        }
      }
    }

    QByteArray body;
    if (contentLength > 0) {
      body = m_buffer.left(contentLength);
      m_buffer.remove(0, contentLength);
    }

    const RtspRequestInfo requestInfo = m_pendingRequests.take(cseq);
    emit responseReceived(cseq, statusCode, statusText, headers, body,
                          requestInfo.method, requestInfo.url,
                          !requestInfo.method.isEmpty());
  }
}

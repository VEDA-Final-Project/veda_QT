#include "srtporchestrator.h"
#include <QDebug>
#include <QAtomicInt>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QThread>

static QAtomicInt s_nextRtpPort(50000); 

namespace {
constexpr quint8 kVideoRtpChannel = 0;
constexpr quint8 kVideoRtcpChannel = 1;
constexpr quint8 kMetaRtpChannel = 2;
constexpr quint8 kMetaRtcpChannel = 3;

bool looksLikePlainMetadataRtp(const QByteArray &packet) {
  if (packet.size() < 12) {
    return false;
  }

  const quint8 v_p_x_cc = static_cast<quint8>(packet[0]);
  const int cc = v_p_x_cc & 0x0F;
  int payloadOffset = 12 + (cc * 4);
  if (packet.size() < payloadOffset) {
    return false;
  }

  if (v_p_x_cc & 0x10) {
    if (packet.size() < payloadOffset + 4) {
      return false;
    }
    const quint16 extLen =
        (static_cast<quint8>(packet[payloadOffset + 2]) << 8) |
        static_cast<quint8>(packet[payloadOffset + 3]);
    payloadOffset += 4 + (extLen * 4);
  }

  if (packet.size() <= payloadOffset) {
    return false;
  }

  const QByteArray payload = packet.mid(payloadOffset);
  return payload.startsWith("<?xml") || payload.startsWith("<tt:")
         || payload.contains("<tt:MetadataStream")
         || payload.contains("<wsnt:NotificationMessage");
}

bool forceMetadataTcpInterleaved() {
  const QString value = QProcessEnvironment::systemEnvironment()
                            .value(QStringLiteral("VEDA_SRTP_FORCE_METADATA_TCP"))
                            .trimmed()
                            .toLower();
  return value == QStringLiteral("1") || value == QStringLiteral("true") ||
         value == QStringLiteral("yes");
}
} // namespace

SrtpOrchestrator::SrtpOrchestrator(QObject *parent)
    : QObject(parent) {
  
  int port = s_nextRtpPort.fetchAndAddRelaxed(4); // 4개씩 건너뜀 (Video 2, Meta 2)
  m_clientRtpPort = port;
  m_clientMetaPort = port + 2;

  m_session = new SrtpSession(this);
  m_rtspClient = new SrtpRtspClient(this);
  m_rtspClient->setSocket(m_session->socket());

  m_decryptor = new SrtpDecryptor(this);
  m_depacketizer = new RtpDepacketizer(this);

  m_decoderThread = new QThread(this);
  m_videoDecoder = new SrtpVideoThread();
  m_videoDecoder->moveToThread(m_decoderThread);

  m_metaParser = new SrtpMetadataParser(this);
  m_keepAliveTimer = new QTimer(this);
  m_keepAliveTimer->setSingleShot(false);
  m_udpSocket = new QUdpSocket(this);
  m_metaUdpSocket = new QUdpSocket(this);

  connect(m_session, &SrtpSession::encrypted, this, &SrtpOrchestrator::onTlsEncrypted);
  connect(m_session, &SrtpSession::error, this, &SrtpOrchestrator::onTlsError);
  connect(m_session, &SrtpSession::disconnected, this,
          &SrtpOrchestrator::onSessionDisconnected);
  connect(m_rtspClient, &SrtpRtspClient::responseReceived, this, &SrtpOrchestrator::onRtspResponse);
  connect(m_rtspClient, &SrtpRtspClient::interleavedDataReceived, this,
          &SrtpOrchestrator::onInterleavedDataReceived);
  connect(m_udpSocket, &QUdpSocket::readyRead, this, &SrtpOrchestrator::onUdpReadyRead);
  connect(m_metaUdpSocket, &QUdpSocket::readyRead, this, &SrtpOrchestrator::onMetaUdpReadyRead);
  connect(m_keepAliveTimer, &QTimer::timeout, this,
          &SrtpOrchestrator::onKeepAliveTimeout);
  connect(m_depacketizer, &RtpDepacketizer::frameReady, m_videoDecoder, &SrtpVideoThread::decodeFrame, Qt::QueuedConnection);
  connect(m_depacketizer, &RtpDepacketizer::metadataReady, m_metaParser, &SrtpMetadataParser::parse);
  connect(m_videoDecoder, &SrtpVideoThread::frameCaptured, this, &SrtpOrchestrator::frameCaptured);
  connect(m_videoDecoder, &SrtpVideoThread::decoderLostSync, m_depacketizer,
          &RtpDepacketizer::requestVideoResync, Qt::QueuedConnection);
  connect(m_metaParser, &SrtpMetadataParser::metadataReceived, this, &SrtpOrchestrator::metadataReceived);

  m_decoderThread->start();
}

SrtpOrchestrator::~SrtpOrchestrator() {
  stop();
  m_decoderThread->quit();
  m_decoderThread->wait();
  delete m_videoDecoder;
}

void SrtpOrchestrator::setConnectionInfo(const QString &ip, const QString &user, const QString &password, const QString &profile) {
  m_ip = ip; m_user = user; m_password = password; m_profile = profile;
}

QString SrtpOrchestrator::baseUrl() const {
  return QString("rtsps://%1:322/%2").arg(m_ip, m_profile);
}

QString SrtpOrchestrator::expectedMethodForPhase() const {
  switch (m_phase) {
    case HandshakePhase::AwaitingOptions:
      return QStringLiteral("OPTIONS");
    case HandshakePhase::AwaitingDescribe:
      return QStringLiteral("DESCRIBE");
    case HandshakePhase::AwaitingSetupVideo:
      return QStringLiteral("SETUP");
    case HandshakePhase::AwaitingSetupMetadata:
      return QStringLiteral("SETUP");
    case HandshakePhase::AwaitingPlay:
      return QStringLiteral("PLAY");
    default:
      return QString();
  }
}

void SrtpOrchestrator::clearNegotiationState() {
  m_sessionId.clear();
  m_videoTrackUrl.clear();
  m_metaTrackUrl.clear();
  m_pendingTransport.clear();
  m_pendingMetaTransport.clear();
  m_metadataRequire = QStringLiteral("ReID");
  m_videoUsesSrtp = true;
  m_metadataUsesSrtp = false;
  m_videoUsesInterleaved = false;
  m_metadataUsesInterleaved = false;
  m_videoCodecId = AV_CODEC_ID_H264;
  m_hasRtspSession = false;
  m_metadataSetupRetriedWithoutRequire = false;
  if (m_keepAliveTimer) {
    m_keepAliveTimer->stop();
  }
  m_keepAliveIntervalMs = 20000;
  m_keys = MikeyBuilder::MikeyKeys();
}

void SrtpOrchestrator::bindUdpSockets() {
  m_udpSocket->close();
  m_metaUdpSocket->close();
  if (!m_udpSocket->bind(QHostAddress::AnyIPv4, m_clientRtpPort)) {
    qWarning() << "[SRTP] Could not bind UDP port" << m_clientRtpPort;
  }
  if (!m_metaUdpSocket->bind(QHostAddress::AnyIPv4, m_clientMetaPort)) {
    qWarning() << "[SRTP] Could not bind metadata UDP port" << m_clientMetaPort;
  }
}

bool SrtpOrchestrator::isRunning() const {
  return m_phase != HandshakePhase::Idle && m_phase != HandshakePhase::Failed;
}

void SrtpOrchestrator::start() {
  m_stopRequested = false;
  clearNegotiationState();
  m_videoPreferTcpInterleaved = true;
  m_metadataPreferUdp = !forceMetadataTcpInterleaved();
  m_phase = HandshakePhase::ConnectingTls;
  bindUdpSockets();
  m_rtspClient->setCredentials(m_user, m_password);
  m_session->connectToCamera(m_ip, 322);
}

void SrtpOrchestrator::stop() {
  m_stopRequested = true;
  if (m_hasRtspSession && !m_sessionId.isEmpty() && m_session->isEncrypted()) {
    m_rtspClient->sendTeardown(baseUrl(), m_sessionId);
  }
  m_session->disconnectFromCamera();
  m_udpSocket->close();
  m_metaUdpSocket->close();
  clearNegotiationState();
  m_phase = HandshakePhase::Idle;
}

void SrtpOrchestrator::onTlsEncrypted() {
  m_phase = HandshakePhase::AwaitingOptions;
  m_rtspClient->sendOptions(baseUrl());
}

void SrtpOrchestrator::onTlsError(const QString &msg) { emit logMessage("[SRTP] TLS Error: " + msg); }

void SrtpOrchestrator::onSessionDisconnected() {
  clearNegotiationState();
  m_phase = m_stopRequested ? HandshakePhase::Idle : HandshakePhase::Failed;
}

void SrtpOrchestrator::failHandshake(const QString &reason, int statusCode) {
  qWarning() << "[SRTP] Handshake failed:" << reason;
  emit logMessage(statusCode > 0
                      ? QString("[SRTP] Handshake failed (%1): %2")
                            .arg(statusCode)
                            .arg(reason)
                      : QString("[SRTP] Handshake failed: %1").arg(reason));
  clearNegotiationState();
  m_phase = HandshakePhase::Failed;
  m_session->disconnectFromCamera();
}

void SrtpOrchestrator::sendPendingSetupRequest() {
  if (m_phase == HandshakePhase::AwaitingSetupMetadata &&
      !m_metaTrackUrl.isEmpty() && !m_pendingMetaTransport.isEmpty() &&
      !m_sessionId.isEmpty()) {
    m_rtspClient->sendSetup(m_metaTrackUrl, m_pendingMetaTransport, QByteArray(),
                            true, m_sessionId, m_metadataRequire);
    return;
  }

  if (!m_videoTrackUrl.isEmpty() && !m_pendingTransport.isEmpty()) {
    m_phase = HandshakePhase::AwaitingSetupVideo;
    m_rtspClient->sendSetup(m_videoTrackUrl, m_pendingTransport, m_keys.mikeyBlob,
                            false);
  }
}

bool SrtpOrchestrator::retryMetadataSetupWithoutRequire(
    int statusCode, const QString &statusText,
    const QMap<QString, QString> &headers) {
  const QString unsupported = headers.value(QStringLiteral("Unsupported"));
  if (m_metadataSetupRetriedWithoutRequire ||
      !(unsupported.contains(QStringLiteral("ReID"), Qt::CaseInsensitive) ||
        statusCode == 551)) {
    return false;
  }

  m_metadataSetupRetriedWithoutRequire = true;
  m_metadataRequire.clear();
  m_rtspClient->sendSetup(m_metaTrackUrl, m_pendingMetaTransport, QByteArray(),
                          true, m_sessionId, m_metadataRequire);
  return true;
}

void SrtpOrchestrator::configurePendingTransports() {
  if (m_videoPreferTcpInterleaved) {
    m_pendingTransport = QStringLiteral("RTP/SAVP/TCP;unicast;interleaved=0-1");
  } else {
    m_pendingTransport = QString("RTP/SAVP/UDP; unicast; client_port=%1-%2")
                             .arg(m_clientRtpPort)
                             .arg(m_clientRtpPort + 1);
  }

  if (m_metadataPreferUdp) {
    m_pendingMetaTransport = QString("RTP/AVP/UDP; unicast; client_port=%1-%2")
                                 .arg(m_clientMetaPort)
                                 .arg(m_clientMetaPort + 1);
  } else {
    m_pendingMetaTransport =
        QStringLiteral("RTP/AVP/TCP;unicast;interleaved=2-3");
  }
}

void SrtpOrchestrator::applyTransportSelection(bool isMetadata,
                                               const QString &transport) {
  const bool usesSrtp =
      transport.contains(QStringLiteral("SAVP"), Qt::CaseInsensitive);
  const bool usesInterleaved =
      transport.contains(QStringLiteral("interleaved="), Qt::CaseInsensitive);

  if (isMetadata) {
    m_metadataUsesSrtp = usesSrtp;
    m_metadataUsesInterleaved = usesInterleaved;
    if (!m_metadataUsesInterleaved) {
      m_metadataUsesSrtp = false;
    }
    return;
  }

  m_videoUsesSrtp = usesSrtp;
  m_videoUsesInterleaved = usesInterleaved;
}

void SrtpOrchestrator::processVideoPacket(const QByteArray &packet) {
  const QByteArray rtpPacket =
      m_videoUsesSrtp ? m_decryptor->decrypt(packet) : packet;
  if (!rtpPacket.isEmpty()) {
    m_depacketizer->processPacket(rtpPacket);
  }
}

void SrtpOrchestrator::processMetadataPacket(const QByteArray &packet) {
  QByteArray metaPacket = packet;
  const bool bypassMetadataDecrypt =
      m_metadataUsesInterleaved && forceMetadataTcpInterleaved();
  if (!bypassMetadataDecrypt && m_metadataUsesSrtp &&
      !looksLikePlainMetadataRtp(packet)) {
    metaPacket = m_decryptor->decrypt(packet);
  }
  if (!metaPacket.isEmpty()) {
    m_depacketizer->processPacket(metaPacket);
  }
}

void SrtpOrchestrator::onRtspResponse(int cseq,
                                      int statusCode,
                                      const QString &statusText,
                                      const QMap<QString, QString> &headers,
                                      const QByteArray &body,
                                      const QString &requestMethod,
                                      const QString &requestUrl,
                                      bool matchedRequest) {
  Q_UNUSED(cseq);
  Q_UNUSED(requestUrl);
  const QString effectiveMethod =
      matchedRequest ? requestMethod : expectedMethodForPhase();

  if (statusCode == 401) {
    if (effectiveMethod == QStringLiteral("OPTIONS")) {
      m_phase = HandshakePhase::AwaitingOptions;
      m_rtspClient->sendOptions(baseUrl());
    } else if (effectiveMethod == QStringLiteral("DESCRIBE")) {
      m_phase = HandshakePhase::AwaitingDescribe;
      m_rtspClient->sendDescribe(baseUrl(), QByteArray());
    } else if (effectiveMethod == QStringLiteral("SETUP") &&
               !m_videoTrackUrl.isEmpty() && !m_pendingTransport.isEmpty()) {
      sendPendingSetupRequest();
    } else if (effectiveMethod == QStringLiteral("PLAY") &&
               !m_sessionId.isEmpty()) {
      m_phase = HandshakePhase::AwaitingPlay;
      m_rtspClient->sendPlay(baseUrl(), m_sessionId);
    } else {
      failHandshake(QStringLiteral("401 arrived without a recoverable pending request."),
                    statusCode);
    }
    return;
  }
  if (statusCode == 459) {
    failHandshake(
        QStringLiteral("459 Aggregate operation not allowed. Track URL was incorrect."),
        statusCode);
    return;
  }
  if (m_phase == HandshakePhase::AwaitingSetupMetadata && statusCode != 200) {
    if (retryMetadataSetupWithoutRequire(statusCode, statusText, headers)) {
      return;
    }

    qWarning() << "[SRTP][Meta] Metadata SETUP rejected:" << statusCode
               << statusText << "Continuing video-only session.";
    m_phase = HandshakePhase::AwaitingPlay;
    m_rtspClient->sendPlay(baseUrl(), m_sessionId);
    return;
  }
  if (statusCode != 200) {
    failHandshake(
        QStringLiteral("Unexpected response during %1: %2 %3")
            .arg(effectiveMethod.isEmpty() ? QStringLiteral("unknown")
                                           : effectiveMethod)
            .arg(statusCode)
            .arg(statusText),
        statusCode);
    return;
  }

  if (effectiveMethod == QStringLiteral("OPTIONS")) {
    m_keys = MikeyBuilder::generate(m_password.toUtf8());
    if (m_keys.mikeyBlob.isEmpty()) {
      failHandshake(QStringLiteral("Failed to build MIKEY payload."), statusCode);
      return;
    }
    m_phase = HandshakePhase::AwaitingDescribe;
    m_rtspClient->sendDescribe(baseUrl(), QByteArray());
  } else if (effectiveMethod == QStringLiteral("DESCRIBE")) {
    handleDescribeResponse(headers, body);
  } else if (effectiveMethod == QStringLiteral("SETUP")) {
    handleSetupResponse(headers);
  } else if (effectiveMethod == QStringLiteral("PLAY")) {
    handlePlayResponse(headers);
  } else if (effectiveMethod == QStringLiteral("GET_PARAMETER")) {
    return;
  } else {
    qWarning() << "[SRTP] Received 200 OK for unhandled RTSP method:"
               << effectiveMethod;
  }
}

void SrtpOrchestrator::handleDescribeResponse(const QMap<QString, QString> &headers, const QByteArray &body) {
  QString sdp = QString::fromUtf8(body);

  QString videoUrl, metaUrl;
  QStringList lines = sdp.split("\n");
  QString currentMediaType; // "video", "application", etc.
  AVCodecID detectedCodecId = AV_CODEC_ID_H264;
  QString detectedCodecName = QStringLiteral("H264");
  QList<QByteArray> detectedParameterSets;

  for (const QString &line : lines) {
    QString clean = line.trimmed();
    
    if (clean.startsWith("m=")) {
      currentMediaType = clean.section(' ', 0, 0).mid(2); // "video", "application"
    } else if (clean.startsWith("a=rtpmap:") && currentMediaType == "video") {
      const QString mapping = clean.mid(QStringLiteral("a=rtpmap:").size());
      const QString codecToken =
          mapping.section(' ', 1, 1).section('/', 0, 0).trimmed().toUpper();
      if (codecToken == QStringLiteral("H265") ||
          codecToken == QStringLiteral("HEVC")) {
        detectedCodecId = AV_CODEC_ID_HEVC;
        detectedCodecName = QStringLiteral("HEVC");
      } else if (codecToken == QStringLiteral("H264")) {
        detectedCodecId = AV_CODEC_ID_H264;
        detectedCodecName = QStringLiteral("H264");
      }
    } else if (clean.startsWith("a=fmtp:") && currentMediaType == "video") {
      const int spropIndex = clean.indexOf(QStringLiteral("sprop-parameter-sets="));
      if (spropIndex != -1) {
        QString value = clean.mid(spropIndex + QStringLiteral("sprop-parameter-sets=").size());
        value = value.section(';', 0, 0).trimmed();
        const QStringList parts = value.split(',', Qt::SkipEmptyParts);
        for (const QString &part : parts) {
          const QByteArray nal = QByteArray::fromBase64(part.trimmed().toLatin1());
          if (!nal.isEmpty()) {
            detectedParameterSets.push_back(nal);
          }
        }
      }
    } else if (clean.startsWith("a=control:") && !currentMediaType.isEmpty()) {
      QString track = clean.mid(10);
      if (track == "*") continue; // 세션 레벨 control은 무시
      
      if (!track.startsWith("rtsp")) {
        if (track.startsWith('/')) 
          track = QString("rtsps://%1:322%2").arg(m_ip, track);
        else 
          track = QString("rtsps://%1:322/%2/%3").arg(m_ip, m_profile, track);
      } else {
        track.replace("rtsp://", "rtsps://");
      }

      if (currentMediaType == "video" && videoUrl.isEmpty()) {
        videoUrl = track;
      } else if (currentMediaType == "application" && metaUrl.isEmpty()) {
        metaUrl = track;
      }
    }
  }

  if (videoUrl.isEmpty()) videoUrl = QString("rtsps://%1:322/%2/trackID=v").arg(m_ip, m_profile);
  if (metaUrl.isEmpty()) metaUrl = QString("rtsps://%1:322/%2/trackID=m").arg(m_ip, m_profile);
  
  m_videoTrackUrl = videoUrl;
  m_metaTrackUrl = metaUrl;
  m_videoCodecId = detectedCodecId;

  m_depacketizer->setH264ParameterSets(detectedParameterSets);

  QMetaObject::invokeMethod(
      m_videoDecoder,
      [decoder = m_videoDecoder, codecId = m_videoCodecId]() { decoder->initCodec(codecId); },
      Qt::QueuedConnection);

  if (m_keys.mikeyBlob.isEmpty()) {
    m_keys = MikeyBuilder::generate(m_password.toUtf8());
  }
  if (m_keys.mikeyBlob.isEmpty()) {
    failHandshake(QStringLiteral("MIKEY payload is empty after DESCRIBE."), 0);
    return;
  }
  
  configurePendingTransports();
  m_phase = HandshakePhase::AwaitingSetupVideo;
  m_rtspClient->sendSetup(m_videoTrackUrl, m_pendingTransport, m_keys.mikeyBlob,
                          false);
}

void SrtpOrchestrator::handleSetupResponse(const QMap<QString, QString> &headers) {
  QString responseSessionId = headers.value("Session");
  const QString sessionHeader = responseSessionId;
  const QString transport = headers.value(QStringLiteral("Transport"));
  int sep = responseSessionId.indexOf(';');
  if (sep != -1) responseSessionId = responseSessionId.left(sep);
  if (responseSessionId.isEmpty()) {
    failHandshake(QStringLiteral("SETUP succeeded without a Session header."), 200);
    return;
  }

  if (m_phase == HandshakePhase::AwaitingSetupVideo) {
    m_sessionId = responseSessionId;
    updateKeepAliveInterval(sessionHeader);
    applyTransportSelection(false, transport);
    if (!m_decryptor->init(m_keys.masterKey, m_keys.masterSalt, m_keys.mkiId)) {
      failHandshake(QStringLiteral("Failed to initialize SRTP decryptor after SETUP."),
                    200);
      return;
    }
    m_hasRtspSession = true;

    if (!m_metaTrackUrl.isEmpty()) {
      m_phase = HandshakePhase::AwaitingSetupMetadata;
      m_rtspClient->sendSetup(m_metaTrackUrl, m_pendingMetaTransport, QByteArray(),
                              true, m_sessionId, m_metadataRequire);
      return;
    }
  } else if (m_phase == HandshakePhase::AwaitingSetupMetadata) {
    applyTransportSelection(true, transport);
    if (forceMetadataTcpInterleaved() && !m_metadataUsesInterleaved) {
      qWarning() << "[SRTP][Meta] Metadata transport did not stay on TLS "
                    "interleaved channel:"
                 << transport;
    }
    if (m_sessionId != responseSessionId) {
      qWarning() << "[SRTP][Step2] Metadata SETUP returned different Session id:"
                 << responseSessionId << "expected:" << m_sessionId;
    }
  }

  m_phase = HandshakePhase::AwaitingPlay;
  m_rtspClient->sendPlay(baseUrl(), m_sessionId);
}

void SrtpOrchestrator::handlePlayResponse(const QMap<QString, QString> &headers) {
  Q_UNUSED(headers);
  m_phase = HandshakePhase::Streaming;
  if (m_keepAliveTimer && !m_sessionId.isEmpty()) {
    m_keepAliveTimer->start(m_keepAliveIntervalMs);
  }
}

void SrtpOrchestrator::onKeepAliveTimeout() {
  if (m_phase != HandshakePhase::Streaming || m_sessionId.isEmpty() ||
      !m_session->isEncrypted()) {
    return;
  }
  m_rtspClient->sendGetParameter(baseUrl(), m_sessionId);
}

void SrtpOrchestrator::updateKeepAliveInterval(const QString &sessionHeader) {
  static const QRegularExpression timeoutRe(QStringLiteral("timeout=(\\d+)"),
                                            QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = timeoutRe.match(sessionHeader);
  if (!match.hasMatch()) {
    m_keepAliveIntervalMs = 20000;
    return;
  }

  const int timeoutSec = match.captured(1).toInt();
  if (timeoutSec <= 0) {
    m_keepAliveIntervalMs = 20000;
    return;
  }

  const int keepAliveSec = qMax(10, timeoutSec / 2);
  m_keepAliveIntervalMs = keepAliveSec * 1000;
}

void SrtpOrchestrator::onUdpReadyRead() {
  if (m_videoUsesInterleaved) {
    return;
  }
  while (m_udpSocket->hasPendingDatagrams()) {
    QByteArray datagram;
    datagram.resize(m_udpSocket->pendingDatagramSize());
    m_udpSocket->readDatagram(datagram.data(), datagram.size());
    if (m_phase != HandshakePhase::Streaming) {
      continue;
    }
    processVideoPacket(datagram);
  }
}

void SrtpOrchestrator::onMetaUdpReadyRead() {
  if (m_metadataUsesInterleaved) {
    return;
  }
  while (m_metaUdpSocket->hasPendingDatagrams()) {
    QByteArray datagram;
    datagram.resize(m_metaUdpSocket->pendingDatagramSize());
    m_metaUdpSocket->readDatagram(datagram.data(), datagram.size());
    if (m_phase != HandshakePhase::Streaming) {
      continue;
    }
    processMetadataPacket(datagram);
  }
}

void SrtpOrchestrator::onInterleavedDataReceived(quint8 channel,
                                                 const QByteArray &data) {
  if (m_phase != HandshakePhase::Streaming) {
    return;
  }

  switch (channel) {
    case kVideoRtpChannel: {
      if (!m_videoUsesInterleaved) {
        break;
      }
      processVideoPacket(data);
      break;
    }
    case kMetaRtpChannel: {
      if (!m_metadataUsesInterleaved) {
        break;
      }
      processMetadataPacket(data);
      break;
    }
    case kVideoRtcpChannel:
    case kMetaRtcpChannel:
      break;
    default:
      qWarning() << "[SRTP][TCP] Unexpected interleaved channel:" << channel
                 << "len:" << data.size();
      break;
  }
}

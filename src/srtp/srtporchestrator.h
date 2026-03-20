#ifndef SRTPORCHESTRATOR_H
#define SRTPORCHESTRATOR_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUdpSocket>
#include <QThread>
#include <QList>
#include <QtGlobal>
#include "srtpsession.h"
#include "srtprtspclient.h"
#include "srtpdecryptor.h"
#include "rtpdepacketizer.h"
#include "srtpvideothread.h"
#include "srtpmetadataparser.h"
#include "mikeybuilder.h"
#include "metadata/metadatathread.h" // ObjectInfo

class SrtpOrchestrator : public QObject {
  Q_OBJECT
public:
  enum class HandshakePhase {
    Idle,
    ConnectingTls,
    AwaitingOptions,
    AwaitingDescribe,
    AwaitingSetupVideo,
    AwaitingSetupMetadata,
    AwaitingPlay,
    Streaming,
    Failed
  };

  explicit SrtpOrchestrator(QObject *parent = nullptr);
  virtual ~SrtpOrchestrator();

  void setConnectionInfo(const QString &ip, const QString &user, const QString &password, const QString &profile);
  void start();
  void stop();
  bool isRunning() const;

signals:
  void frameCaptured(QSharedPointer<cv::Mat> framePtr, qint64 timestampMs);

  void metadataReceived(const QList<ObjectInfo> &objects);
  void logMessage(const QString &msg);

private slots:
  void onTlsEncrypted();
  void onTlsError(const QString &msg);
  void onSessionDisconnected();
  void onRtspResponse(int cseq,
                      int statusCode,
                      const QString &statusText,
                      const QMap<QString, QString> &headers,
                      const QByteArray &body,
                      const QString &requestMethod,
                      const QString &requestUrl,
                      bool matchedRequest);
  void onInterleavedDataReceived(quint8 channel, const QByteArray &data);
  void onUdpReadyRead();
  void onMetaUdpReadyRead();
  void onKeepAliveTimeout();

private:
  QString baseUrl() const;
  QString expectedMethodForPhase() const;
  void clearNegotiationState();
  void bindUdpSockets();
  void failHandshake(const QString &reason, int statusCode = 0);
  void handleDescribeResponse(const QMap<QString, QString> &headers, const QByteArray &body);
  void handleSetupResponse(const QMap<QString, QString> &headers);
  void handlePlayResponse(const QMap<QString, QString> &headers);
  void sendPendingSetupRequest();
  bool retryMetadataSetupWithoutRequire(int statusCode,
                                        const QString &statusText,
                                        const QMap<QString, QString> &headers);
  void configurePendingTransports();
  void applyTransportSelection(bool isMetadata, const QString &transport);
  void processVideoPacket(const QByteArray &packet);
  void processMetadataPacket(const QByteArray &packet);
  void updateKeepAliveInterval(const QString &sessionHeader);

  SrtpSession *m_session;
  SrtpRtspClient *m_rtspClient;
  SrtpDecryptor *m_decryptor;
  RtpDepacketizer *m_depacketizer;
  SrtpVideoThread *m_videoDecoder;
  QThread *m_decoderThread;
  SrtpMetadataParser *m_metaParser;
  QTimer *m_keepAliveTimer;
  QUdpSocket *m_udpSocket;
  QUdpSocket *m_metaUdpSocket;

  QString m_ip, m_user, m_password, m_profile;
  QString m_sessionId;
  QString m_videoTrackUrl;
  QString m_metaTrackUrl;
  QString m_pendingTransport;
  QString m_pendingMetaTransport;
  QString m_metadataRequire;
  int m_clientRtpPort;
  int m_clientMetaPort;
  bool m_videoPreferTcpInterleaved = true;
  bool m_metadataPreferUdp = true;
  bool m_videoUsesSrtp = true;
  bool m_metadataUsesSrtp = false;
  bool m_videoUsesInterleaved = false;
  bool m_metadataUsesInterleaved = false;
  AVCodecID m_videoCodecId = AV_CODEC_ID_H264;
  HandshakePhase m_phase = HandshakePhase::Idle;
  bool m_hasRtspSession = false;
  bool m_stopRequested = false;
  bool m_metadataSetupRetriedWithoutRequire = false;
  int m_keepAliveIntervalMs = 20000;
  MikeyBuilder::MikeyKeys m_keys;
};

#endif // SRTPORCHESTRATOR_H

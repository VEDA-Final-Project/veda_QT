#ifndef CAMERASOURCE_H
#define CAMERASOURCE_H

#include "infrastructure/camera/camerasessionservice.h"
#include "infrastructure/ocr/plateocrcoordinator.h"
#include "application/parking/parkingservice.h"
#include "application/roi/roiservice.h"
#include "infrastructure/vision/reidextractor.h"
#include "presentation/widgets/videoframerenderer.h"
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <atomic>

class TelegramBotAPI;

class CameraSource : public QObject {
  Q_OBJECT

public:
  enum class Status { Stopped, Connecting, Live, Error };
  Q_ENUM(Status)

  explicit CameraSource(const QString &cameraKey, int cardIndex,
                        QObject *parent = nullptr);

  bool initialize(TelegramBotAPI *telegramApi = nullptr);
  void start();
  void stop();

  void attachDisplayConsumer(int slotId, const QSize &size);
  void detachDisplayConsumer(int slotId);
  void updateConsumerSize(int slotId, const QSize &size);
  void updateObjectFilter(const QSet<QString> &disabledTypes);

  bool reloadRoi(bool writeLog = true);
  void syncEnabledRoiPolygons();

  bool isRunning() const;
  QString cameraKey() const;
  int cardIndex() const;
  QString displayProfile() const;
  Status status() const;
  qint64 lastFrameTimestampMs() const;
  ParkingService *parkingService();
  const ParkingService *parkingService() const;
  RoiService *roiService();
  const RoiService *roiService() const;
  const QVector<QJsonObject> &roiRecords() const;
  QList<VehicleState> activeVehicles() const;
  QList<QPolygonF> normalizedRoiPolygons() const;
  QStringList roiLabels() const;

signals:
  void thumbnailFrameReady(int cardIndex, const QImage &image);
  void displayFrameReady(const QImage &image, const QList<ObjectInfo> &objects);
  void rawFrameReady(int cardIndex, QSharedPointer<cv::Mat> framePtr,
                     qint64 timestampMs);
  void roiDataChanged();
  void videoReady();
  void zoneStateChanged();
  void statusChanged(int cardIndex, CameraSource::Status status,
                     const QString &detail);
  void logMessage(const QString &msg);

private slots:
  void onMetadataReceived(const QList<ObjectInfo> &objects);
  void onFrameCaptured(QSharedPointer<cv::Mat> framePtr, qint64 timestampMs);
  void onOcrResult(int objectId, const OcrFullResult &result);
  void onDisplayRenderTick();
  void onThumbnailRenderTick();
  void onOcrDispatchTick();
  void onReidDispatchTick();
  void onHealthCheck();
  void onReconnectTimeout();

private:
  static QString bestProfileForSize(const QSize &size);
  static QString ocrMasterProfile();
  static int profileRank(const QString &profile);
  static QString higherQualityProfile(const QString &a, const QString &b);

  bool refreshConnectionFromConfig(const QString &displayProfile,
                                   bool reloadConfig);
  QString desiredProfile() const;
  void updateDisplayProfileForConsumers();
  void syncZoneOccupancyFromActiveVehicles();
  void setStatus(Status status, const QString &detail = QString());
  void scheduleReconnect(const QString &reason);
  void clearReconnect();
  void startDisplayStream(bool forceRestart, bool reloadConfig,
                          const QString &reason);

  CameraManager *m_cameraManager = nullptr;
  PlateOcrCoordinator *m_ocrCoordinator = nullptr;
  ParkingService *m_parkingService = nullptr;
  CameraSessionService m_cameraSession;
  RoiService m_roiService;
  ReIDFeatureExtractor m_reidExtractor;
  VideoFrameRenderer m_frameRenderer;
  QString m_cameraKey;
  int m_cardIndex = -1;
  QString m_displayProfile;
  QHash<int, QSize> m_consumerSizes;
  QVector<QString> m_enabledZoneIds;
  QList<ObjectInfo> m_latestFrameObjects;
  QSet<QString> m_disabledTypes;
  QSharedPointer<cv::Mat> m_latestFramePtr;
  bool m_initialized = false;
  bool m_videoReadyNotified = false;
  bool m_shouldRun = false;
  Status m_status = Status::Stopped;
  QString m_statusDetail;
  qint64 m_lastFrameTimestampMs = 0;
  qint64 m_latestBufferedFrameTimestampMs = 0;
  qint64 m_lastStartAttemptMs = 0;
  qint64 m_lastDisplayRenderedTimestampMs = 0;
  qint64 m_lastThumbnailRenderedTimestampMs = 0;
  qint64 m_lastOcrProcessedTimestampMs = 0;
  int m_reconnectAttempt = 0;
  QElapsedTimer m_roiSyncTimer;
  QElapsedTimer m_zoneStatusTimer;
  QElapsedTimer m_frameCapturedTraceTimer;
  QElapsedTimer m_displayRenderTraceTimer;
  QTimer *m_healthTimer = nullptr;
  QTimer *m_reconnectTimer = nullptr;
  QTimer *m_displayRenderTimer = nullptr;
  QTimer *m_thumbnailRenderTimer = nullptr;
  QTimer *m_ocrDispatchTimer = nullptr;
  QTimer *m_reidDispatchTimer = nullptr;
  std::atomic<bool> m_isInitializing{false};
  std::atomic<bool> m_reidProcessing{false};
};

#endif // CAMERASOURCE_H

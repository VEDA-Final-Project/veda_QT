#ifndef CAMERASOURCE_H
#define CAMERASOURCE_H

#include "camera/camerasessionservice.h"
#include "ocr/plateocrcoordinator.h"
#include "parking/parkingservice.h"
#include "roi/roiservice.h"
#include "ui/video/videoframerenderer.h"
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QStringList>

class TelegramBotAPI;

class CameraSource : public QObject {
  Q_OBJECT

public:
  explicit CameraSource(const QString &cameraKey, int cardIndex,
                        QObject *parent = nullptr);

  bool initialize(TelegramBotAPI *telegramApi = nullptr);
  void start();
  void stop();

  void attachDisplayConsumer(int slotId, const QSize &size);
  void detachDisplayConsumer(int slotId);
  void updateConsumerSize(int slotId, const QSize &size);
  void setAnalyticsActive(int slotId, bool active);
  void updateObjectFilter(const QSet<QString> &disabledTypes);

  bool reloadRoi(bool writeLog = true);
  void syncEnabledRoiPolygons();

  bool isRunning() const;
  QString cameraKey() const;
  int cardIndex() const;
  QString displayProfile() const;
  QString ocrProfile() const;
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
  void roiDataChanged();
  void videoReady();
  void zoneStateChanged();
  void logMessage(const QString &msg);

private slots:
  void onMetadataReceived(const QList<ObjectInfo> &objects);
  void onFrameCaptured(QSharedPointer<cv::Mat> framePtr, qint64 timestampMs);
  void onOcrFrameCaptured(QSharedPointer<cv::Mat> framePtr, qint64 timestampMs);
  void onOcrResult(int objectId, const OcrFullResult &result);

private:
  static QString bestProfileForSize(const QSize &size);

  bool refreshConnectionFromConfig(const QString &displayProfile,
                                   bool reloadConfig);
  QString desiredProfile() const;
  void updateDisplayProfileForConsumers();
  void updateAnalyticsState();
  void syncZoneOccupancyFromActiveVehicles();

  CameraManager *m_cameraManager = nullptr;
  PlateOcrCoordinator *m_ocrCoordinator = nullptr;
  ParkingService *m_parkingService = nullptr;
  CameraSessionService m_cameraSession;
  RoiService m_roiService;
  VideoFrameRenderer m_frameRenderer;
  QString m_cameraKey;
  int m_cardIndex = -1;
  QString m_displayProfile;
  QString m_idleProfile;
  QString m_ocrProfile;
  QHash<int, QSize> m_consumerSizes;
  QSet<int> m_analyticsSlots;
  QVector<QString> m_enabledZoneIds;
  QList<ObjectInfo> m_currentObjects;
  QSet<QString> m_disabledTypes;
  bool m_initialized = false;
  bool m_videoReadyNotified = false;
  QElapsedTimer m_roiSyncTimer;
  QElapsedTimer m_zoneStatusTimer;
  QElapsedTimer m_frameRenderTimer;
  QElapsedTimer m_thumbnailRenderTimer;
  QElapsedTimer m_ocrDispatchTimer;
};

#endif // CAMERASOURCE_H

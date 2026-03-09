#ifndef CAMERACHANNELRUNTIME_H
#define CAMERACHANNELRUNTIME_H

#include "camera/camerasessionservice.h"
#include "ocr/plateocrcoordinator.h"
#include "parking/parkingservice.h"
#include "roi/roiservice.h"
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>

class QCheckBox;
class QLabel;
class QSpinBox;
class QTableWidget;
class TelegramBotAPI;
class VideoWidget;

class CameraChannelRuntime : public QObject {
  Q_OBJECT

public:
  enum class Slot { Primary, Secondary };

  struct SharedUiRefs {
    QTableWidget *reidTable = nullptr;
    QSpinBox *staleTimeoutInput = nullptr;
    QSpinBox *pruneTimeoutInput = nullptr;
    QCheckBox *chkShowStaleObjects = nullptr;
    QLabel *avgFpsLabel = nullptr;
  };

  CameraChannelRuntime(Slot slot, const QString &channelLabel,
                       VideoWidget *videoWidget,
                       const QString &defaultCameraKey,
                       const SharedUiRefs &sharedUi,
                       QObject *parent = nullptr);

  void connectSignals();
  bool initializeParkingService();
  void setTelegramApi(TelegramBotAPI *telegramApi);
  void shutdown();

  bool activate(const QString &cameraKey, int cardIndex);
  void deactivate();
  void selectCardWithoutStream(int cardIndex);
  void handleResizeProfileChange();
  void updateObjectFilter(const QSet<QString> &disabledTypes);
  void setShowFps(bool show);
  bool reloadRoi(bool writeLog = true);
  void syncEnabledRoiPolygons();
  void setReidPanelActive(bool active);

  int selectedCardIndex() const;
  QString cameraKey() const;
  QString channelLabel() const;
  QString displayProfile() const;
  QString ocrProfile() const;
  VideoWidget *videoWidget() const;
  bool isRunning() const;
  ParkingService *parkingService();
  const ParkingService *parkingService() const;
  RoiService *roiService();
  const RoiService *roiService() const;
  const QVector<QJsonObject> &roiRecords() const;
  QList<VehicleState> activeVehicles() const;

signals:
  void logMessage(const QString &msg);
  void thumbnailFrameReady(int cardIndex, const QImage &image);
  void videoReady();
  void zoneStateChanged();

private slots:
  void onMetadataReceived(const QList<ObjectInfo> &objects);
  void onFrameCaptured(QSharedPointer<cv::Mat> framePtr, qint64 timestampMs);
  void onOcrFrameCaptured(QSharedPointer<cv::Mat> framePtr, qint64 timestampMs);
  void onOcrResult(int objectId, const OcrFullResult &result);

private:
  static void populateReidTable(QTableWidget *table,
                                const QList<VehicleState> &vehicleStates,
                                int staleTimeoutMs, bool showStaleObjects);
  bool refreshConnectionFromConfig(const QString &displayProfile,
                                   bool reloadConfig);
  QString bestProfileForSize(const QSize &size) const;
  void refreshReidTable();
  void clearWidgetState();
  void syncEnabledRoiPolygonsInternal();
  void syncZoneOccupancyFromActiveVehicles();

  Slot m_slot;
  QString m_channelLabel;
  QString m_defaultCameraKey;
  QString m_cameraKey;
  VideoWidget *m_videoWidget = nullptr;
  SharedUiRefs m_sharedUi;
  CameraManager *m_cameraManager = nullptr;
  PlateOcrCoordinator *m_ocrCoordinator = nullptr;
  ParkingService *m_parkingService = nullptr;
  CameraSessionService m_cameraSession;
  RoiService m_roiService;
  QVector<QString> m_enabledZoneIds;
  int m_selectedCardIndex = -1;
  QString m_displayProfile;
  QString m_ocrProfile;
  bool m_signalsConnected = false;
  bool m_videoReadyNotified = false;
  bool m_reidPanelActive = false;
  QElapsedTimer m_roiSyncTimer;
  QElapsedTimer m_zoneStatusTimer;
  QElapsedTimer m_reidTimer;
  QElapsedTimer m_renderTimer;
  QElapsedTimer m_ocrDispatchTimer;
};

#endif // CAMERACHANNELRUNTIME_H

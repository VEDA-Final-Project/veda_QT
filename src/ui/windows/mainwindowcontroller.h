#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include "camera/camerasessionservice.h"
#include "logging/logdeduplicator.h"
#include "mainwindowuirefs.h"
#include "ocr/plateocrcoordinator.h"
#include "parking/parkingservice.h"
#include "roi/roiservice.h"
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPushButton>
#include <QSet>
#include <QTextEdit>

#include "telegram/telegrambotapi.h"

class RpiPanelController;
class DbPanelController;

class MainWindowController : public QObject {
  Q_OBJECT

public:
  explicit MainWindowController(const MainWindowUiRefs &uiRefs,
                                QObject *parent = nullptr);
  void shutdown();

public slots:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void onVideoWidgetResizedSlot();

  void connectSignals();
  void initRoiDb();
  void appendRoiStructuredLog(const QJsonObject &roiData);
  void refreshRoiSelector();
  void playCctv();
  void updateObjectFilter(const QSet<QString> &disabledTypes);
  void onLogMessage(const QString &msg);
  void onOcrResultPrimary(int objectId, const QString &result);
  void onOcrResultSecondary(int objectId, const QString &result);
  void onStartRoiDraw();
  void onCompleteRoiDraw();   // Renamed from onFinishRoiDraw
  void onDeleteSelectedRoi(); // Renamed from onDeleteRoi
  void onRoiChanged(const QRect &roi);
  void onRoiPolygonChanged(const QPolygon &polygon, const QSize &frameSize);
  void onRoiTargetChanged(int index);
  void onViewModeChanged(int index);
  void onCameraPrimarySelectionChanged(int index);
  void onCameraSecondarySelectionChanged(int index);
  void onMetadataReceivedPrimary(const QList<ObjectInfo> &objects);
  void onMetadataReceivedSecondary(const QList<ObjectInfo> &objects);
  void onFrameCapturedPrimary(QSharedPointer<cv::Mat> framePtr,
                              qint64 timestampMs);
  void onFrameCapturedSecondary(QSharedPointer<cv::Mat> framePtr,
                                qint64 timestampMs);
  void onReidTableCellClicked(int row, int column);

  // Telegram Slots
  void onSendEntry();
  void onSendExit();
  void onTelegramLog(const QString &msg);
  void onUsersUpdated(int count);
  void onPaymentConfirmed(const QString &plate, int amount);
  void onAdminSummoned(const QString &chatId, const QString &name);

private:
  enum class ViewMode { Single = 0, Dual = 1 };
  enum class RoiTarget { Primary = 0, Secondary = 1 };

  void refreshCameraSelectors();
  void initRoiDbForChannels();
  void reloadRoiForTarget(RoiTarget target, bool writeLog = true);
  void refreshRoiSelectorForTarget();
  void refreshZoneTableAllChannels();
  void applyViewModeUiState();
  bool refreshCameraConnectionFromConfig(
      CameraManager *cameraManager, const QString &cameraKey,
      QString *resolvedKey = nullptr, const QString &profileSuffix = QString(),
      bool reloadConfig = true);
  QString getBestProfileForWidth(int width) const;
  VideoWidget *videoWidgetForTarget(RoiTarget target) const;
  RoiService *roiServiceForTarget(RoiTarget target);
  const RoiService *roiServiceForTarget(RoiTarget target) const;
  ParkingService *parkingServiceForTarget(RoiTarget target);
  QString cameraKeyForTarget(RoiTarget target) const;

  MainWindowUiRefs m_ui;
  ViewMode m_viewMode = ViewMode::Single;
  RoiTarget m_roiTarget = RoiTarget::Primary;
  QString m_selectedCameraKeyPrimary = QStringLiteral("camera");
  QString m_selectedCameraKeySecondary = QStringLiteral("camera2");
  CameraManager *m_cameraManagerPrimary = nullptr;
  CameraManager *m_cameraManagerSecondary = nullptr;
  PlateOcrCoordinator *m_ocrCoordinatorPrimary = nullptr;
  PlateOcrCoordinator *m_ocrCoordinatorSecondary = nullptr;
  TelegramBotAPI *m_telegramApi = nullptr;
  RpiPanelController *m_rpiPanelController = nullptr;
  DbPanelController *m_dbPanelController = nullptr;
  CameraSessionService m_cameraSessionPrimary;
  CameraSessionService m_cameraSessionSecondary;
  RoiService m_roiServicePrimary;
  RoiService m_roiServiceSecondary;
  ParkingService *m_parkingServicePrimary = nullptr;
  ParkingService *m_parkingServiceSecondary = nullptr;
  LogDeduplicator m_logDeduplicator;
  QElapsedTimer m_renderTimerPrimary;
  QElapsedTimer m_renderTimerSecondary;
  QTimer *m_resizeDebounceTimer = nullptr;
  QString m_currentProfilePrimary;
  QString m_currentProfileSecondary;
};

#endif // MAINWINDOWCONTROLLER_H

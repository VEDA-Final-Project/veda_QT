#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include "camera/camerasessionservice.h"
#include "logging/logdeduplicator.h"
#include "ocr/plateocrcoordinator.h"
#include "parking/parkingservice.h"
#include "roi/roiservice.h"
#include "rpi/rpitcpclient.h"
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPushButton>
#include <QSet>
#include <QTextEdit>

#include "telegram/telegrambotapi.h"

class QLineEdit;
class QSpinBox;
class QLabel;
class VideoWidget;
class QTableWidget;
class QCheckBox;
class QDoubleSpinBox;

class MainWindowController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    VideoWidget *videoWidgetPrimary = nullptr;
    VideoWidget *videoWidgetSecondary = nullptr;
    QComboBox *viewModeCombo = nullptr;
    QComboBox *cameraPrimarySelectorCombo = nullptr;
    QComboBox *cameraSecondarySelectorCombo = nullptr;
    QComboBox *roiTargetCombo = nullptr;
    QLineEdit *roiNameEdit = nullptr;
    QComboBox *roiPurposeCombo = nullptr;
    QComboBox *roiSelectorCombo = nullptr;
    QTextEdit *logView = nullptr;
    QPushButton *btnPlay = nullptr;
    QPushButton *btnApplyRoi = nullptr;
    QPushButton *btnFinishRoi = nullptr;
    QPushButton *btnDeleteRoi = nullptr;

    // Telegram Widgets
    QLabel *userCountLabel = nullptr;
    QLineEdit *entryPlateInput = nullptr;
    QPushButton *btnSendEntry = nullptr;
    QLineEdit *exitPlateInput = nullptr;
    QSpinBox *feeInput = nullptr;
    QPushButton *btnSendExit = nullptr;
    QTableWidget *userTable = nullptr;

    // RPi Widgets
    QLineEdit *rpiHostEdit = nullptr;
    QSpinBox *rpiPortSpin = nullptr;
    QPushButton *btnRpiConnect = nullptr;
    QPushButton *btnRpiDisconnect = nullptr;
    QPushButton *btnBarrierUp = nullptr;
    QPushButton *btnBarrierDown = nullptr;
    QPushButton *btnLedOn = nullptr;
    QPushButton *btnLedOff = nullptr;
    QLabel *rpiConnectionStatusLabel = nullptr;
    QLabel *rpiVehicleStatusLabel = nullptr;
    QLabel *rpiLedStatusLabel = nullptr;
    QLabel *rpiIrRawLabel = nullptr;
    QLabel *rpiServoAngleLabel = nullptr;

    // Parking DB Panel Widgets
    QTableWidget *parkingLogTable = nullptr;
    QLineEdit *plateSearchInput = nullptr;
    QPushButton *btnSearchPlate = nullptr;
    QPushButton *btnRefreshLogs = nullptr;
    QLineEdit *forcePlateInput = nullptr;
    QSpinBox *forceObjectIdInput = nullptr;
    QLineEdit *forceTypeInput = nullptr;
    QDoubleSpinBox *forceScoreInput = nullptr;
    QLineEdit *forceBBoxInput = nullptr;
    QPushButton *btnForcePlate = nullptr;
    QLineEdit *editPlateInput = nullptr;
    QPushButton *btnEditPlate = nullptr;
    QCheckBox *chkShowPlateLogs = nullptr;
    QTableWidget *reidTable = nullptr; // ReID Tab
    QSpinBox *staleTimeoutInput = nullptr;
    QSpinBox *pruneTimeoutInput = nullptr;
    QCheckBox *chkShowStaleObjects = nullptr;

    // New DB sub-tab refs
    QTableWidget *userDbTable = nullptr;
    QPushButton *btnRefreshUsers = nullptr;
    QPushButton *btnDeleteUser = nullptr;

    QTableWidget *hwLogTable = nullptr;
    QPushButton *btnRefreshHwLogs = nullptr;
    QPushButton *btnClearHwLogs = nullptr;

    QTableWidget *vehicleTable = nullptr;
    QPushButton *btnRefreshVehicles = nullptr;
    QPushButton *btnDeleteVehicle = nullptr;

    QTableWidget *zoneTable = nullptr;
    QPushButton *btnRefreshZone = nullptr;
  };

  explicit MainWindowController(const UiRefs &uiRefs,
                                QObject *parent = nullptr);
  void shutdown();

public slots:
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

  // RPi Slots
  void onRpiConnect();
  void onRpiDisconnect();
  void onRpiBarrierUp();
  void onRpiBarrierDown();
  void onRpiLedOn();
  void onRpiLedOff();
  void onRpiConnectedChanged(bool connected);
  void onRpiParkingStatusUpdated(bool vehicleDetected, bool ledOn, int irRaw,
                                 int servoAngle);
  void onRpiAckReceived(const QString &messageId);
  void onRpiErrReceived(const QString &messageId, const QString &code,
                        const QString &message);
  void onRpiLogMessage(const QString &message);

  // Parking DB Slots
  void onRefreshParkingLogs();
  void onSearchParkingLogs();
  void onForcePlate();
  void onEditPlate();

  // New DB CRUD slots
  void refreshParkingLogs();
  void deleteParkingLog();
  void refreshUserTable();
  void deleteUser();
  void refreshHwLogs();
  void clearHwLogs();
  void refreshVehicleTable();
  void deleteVehicle();
  void refreshZoneTable();

private:
  enum class ViewMode { Single = 0, Dual = 1 };
  enum class RoiTarget { Primary = 0, Secondary = 1 };

  void refreshCameraSelectors();
  void initRoiDbForChannels();
  void reloadRoiForTarget(RoiTarget target, bool writeLog = true);
  void refreshRoiSelectorForTarget();
  void refreshZoneTableAllChannels();
  void applyViewModeUiState();
  bool refreshCameraConnectionFromConfig(CameraManager *cameraManager,
                                         const QString &cameraKey,
                                         QString *resolvedKey = nullptr);
  VideoWidget *videoWidgetForTarget(RoiTarget target) const;
  RoiService *roiServiceForTarget(RoiTarget target);
  const RoiService *roiServiceForTarget(RoiTarget target) const;
  ParkingService *parkingServiceForTarget(RoiTarget target);
  QString cameraKeyForTarget(RoiTarget target) const;

  UiRefs m_ui;
  ViewMode m_viewMode = ViewMode::Single;
  RoiTarget m_roiTarget = RoiTarget::Primary;
  QString m_selectedCameraKeyPrimary = QStringLiteral("camera");
  QString m_selectedCameraKeySecondary = QStringLiteral("camera2");
  CameraManager *m_cameraManagerPrimary = nullptr;
  CameraManager *m_cameraManagerSecondary = nullptr;
  PlateOcrCoordinator *m_ocrCoordinatorPrimary = nullptr;
  PlateOcrCoordinator *m_ocrCoordinatorSecondary = nullptr;
  TelegramBotAPI *m_telegramApi = nullptr;
  RpiTcpClient *m_rpiClient = nullptr;
  CameraSessionService m_cameraSessionPrimary;
  CameraSessionService m_cameraSessionSecondary;
  RoiService m_roiServicePrimary;
  RoiService m_roiServiceSecondary;
  ParkingService *m_parkingServicePrimary = nullptr;
  ParkingService *m_parkingServiceSecondary = nullptr;
  LogDeduplicator m_logDeduplicator;
  QElapsedTimer m_renderTimerPrimary;
  QElapsedTimer m_renderTimerSecondary;
};

#endif // MAINWINDOWCONTROLLER_H

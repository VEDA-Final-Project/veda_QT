#ifndef MAINWINDOWUIREFS_H
#define MAINWINDOWUIREFS_H

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class VideoWidget;
class QFrame;
class QGridLayout;

struct MainWindowUiRefs {
  VideoWidget *videoWidgets[4] = {nullptr, nullptr, nullptr, nullptr};
  QFrame *channelCards[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *channelStatusDots[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *channelNameLabels[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *thumbnailLabels[4] = {nullptr, nullptr, nullptr, nullptr};
  QGridLayout *videoGridLayout = nullptr;
  QComboBox *roiTargetCombo = nullptr;
  QLineEdit *roiNameEdit = nullptr;
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

  // RPi 제어신호 수신 클라이언트 위젯
  QLineEdit   *rpiHostEdit              = nullptr; // RPi IP
  QSpinBox    *rpiPortSpin              = nullptr; // 포트 (기본 12345)
  QPushButton *btnRpiConnect            = nullptr;
  QPushButton *btnRpiDisconnect         = nullptr;
  QLabel      *rpiConnectionStatusLabel = nullptr; // CONNECTED / DISCONNECTED
  QLabel      *rpiLastCmdLabel          = nullptr; // 마지막 수신 패킷
  QTextEdit   *rpiCtrlLogView           = nullptr; // 수신 로그

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
  QCheckBox *chkShowFps = nullptr;
  QLabel *lblAvgFps = nullptr;
  QTableWidget *reidTable = nullptr;
  QSpinBox *staleTimeoutInput = nullptr;
  QSpinBox *pruneTimeoutInput = nullptr;
  QCheckBox *chkShowStaleObjects = nullptr;

  // New DB sub-tab refs
  QTableWidget *userDbTable = nullptr;
  QPushButton *btnRefreshUsers = nullptr;
  QPushButton *btnAddUser = nullptr;
  QPushButton *btnEditUser = nullptr;
  QPushButton *btnDeleteUser = nullptr;
  QTableWidget *hwLogTable = nullptr;
  QPushButton *btnRefreshHwLogs = nullptr;
  QPushButton *btnClearHwLogs = nullptr;
  QTableWidget *vehicleTable = nullptr;
  QPushButton *btnRefreshVehicles = nullptr;
  QPushButton *btnDeleteVehicle = nullptr;
  QTableWidget *zoneTable = nullptr;
  QPushButton *btnRefreshZone = nullptr;

  // CCTV Event Log Panel
  QListWidget *eventListWidget = nullptr;

  QPushButton *btnCaptureManual = nullptr;
  QPushButton *btnRecordManual = nullptr;

  // Recording Search Panel
  QTableWidget *recordLogTable = nullptr;
  QPushButton *btnRefreshRecordLogs = nullptr;
  QPushButton *btnDeleteRecordLog = nullptr;
  VideoWidget *recordVideoWidget = nullptr;
  QLineEdit *recordEventTypeInput = nullptr;
  QSpinBox *recordIntervalSpin = nullptr;
  QPushButton *btnApplyEventSetting = nullptr;
  QPushButton *btnTriggerEventRecord = nullptr;
  QComboBox *cmbManualCamera = nullptr;
  QPushButton *btnCaptureRecordTab = nullptr;
  QPushButton *btnRecordRecordTab = nullptr;
  QLabel *recordPreviewPathLabel = nullptr;

  // Video player controls
  QPushButton *btnVideoPlay = nullptr;
  QPushButton *btnVideoPause = nullptr;
  QPushButton *btnVideoStop = nullptr;
  QSlider *videoSeekSlider = nullptr;
  QLabel *videoTimeLabel = nullptr;

  // Continuous Recording (상시 녹화)
  QSpinBox *spinRecordRetention = nullptr;
  QLabel *lblContinuousStatus = nullptr;
  QPushButton *btnApplyContinuousSetting = nullptr;
  QPushButton *btnViewContinuous = nullptr;
};

#endif // MAINWINDOWUIREFS_H

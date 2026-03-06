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

struct MainWindowUiRefs {
  VideoWidget *videoWidgetPrimary = nullptr;
  VideoWidget *videoWidgetSecondary = nullptr;
  QFrame *channelCards[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *channelStatusDots[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *channelNameLabels[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *thumbnailLabels[4] = {nullptr, nullptr, nullptr, nullptr};
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
  QSpinBox *recordPreSecSpin = nullptr;
  QSpinBox *recordPostSecSpin = nullptr;
  QPushButton *btnTriggerEventRecord = nullptr;
  QComboBox *cmbManualCamera = nullptr;
  QPushButton *btnCaptureRecordTab = nullptr;
  QPushButton *btnRecordRecordTab = nullptr;
  QLabel *recordStatusLabel = nullptr;
  QLabel *recordPreviewPathLabel = nullptr;

  // Video player controls
  QPushButton *btnVideoPlay = nullptr;
  QPushButton *btnVideoPause = nullptr;
  QPushButton *btnVideoStop = nullptr;
  QSlider *videoSeekSlider = nullptr;
  QLabel *videoTimeLabel = nullptr;

  // Continuous Recording (상시 녹화)
  QCheckBox *chkContinuousEnable = nullptr;
  QSpinBox *spinRecordRetention = nullptr;
  QSpinBox *spinRecordInterval = nullptr;
  QLabel *lblContinuousStatus = nullptr;
  QPushButton *btnViewContinuous = nullptr;
};

#endif // MAINWINDOWUIREFS_H

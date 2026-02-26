#ifndef MAINWINDOWUIREFS_H
#define MAINWINDOWUIREFS_H

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class VideoWidget;

struct MainWindowUiRefs {
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
  QTableWidget *reidTable = nullptr;
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

#endif // MAINWINDOWUIREFS_H

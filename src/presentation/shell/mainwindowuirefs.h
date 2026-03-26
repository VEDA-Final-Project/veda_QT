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
class QMenu;
class QStackedWidget;
class QTabWidget;
class QToolButton;

struct HeaderUiRefs {
  QLabel *headerIconLabel = nullptr;
  QLabel *headerTitleLabel = nullptr;
  QPushButton *menuButton = nullptr;
  QMenu *navMenu = nullptr;
  // Moved settingsButton to CctvUiRefs
  QPushButton *btnMinimize = nullptr;
  QPushButton *btnMaxRestore = nullptr;
  QPushButton *btnExit = nullptr;
};

struct SplashUiRefs {
  QLabel *titleLabel = nullptr;
  QLabel *messageLabel = nullptr;
};

struct CctvUiRefs {
  VideoWidget *videoWidgets[4] = {nullptr, nullptr, nullptr, nullptr};
  QFrame *channelCards[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *channelStatusDots[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *channelNameLabels[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *thumbnailLabels[4] = {nullptr, nullptr, nullptr, nullptr};
  QGridLayout *videoGridLayout = nullptr;
  QComboBox *roiTargetCombo = nullptr;
  QLineEdit *roiNameEdit = nullptr;
  QComboBox *roiSelectorCombo = nullptr;
  QPushButton *btnApplyRoi = nullptr;
  QPushButton *btnFinishRoi = nullptr;
  QPushButton *btnDeleteRoi = nullptr;
  QCheckBox *chkVehicle = nullptr;
  QCheckBox *chkPlate = nullptr;
  QCheckBox *chkOther = nullptr;
  QPushButton *chkShowFps = nullptr;
  QLabel *lblAvgFps = nullptr;
  QPushButton *btnCaptureManual = nullptr;
  QPushButton *btnRecordManual = nullptr;
  QLabel *footerTimeLabel = nullptr;
  QPushButton *settingsButton = nullptr;
  
  // PTZ Control Buttons
  QPushButton *btnPtzUp = nullptr;
  QPushButton *btnPtzDown = nullptr;
  QPushButton *btnPtzLeft = nullptr;
  QPushButton *btnPtzRight = nullptr;
  QPushButton *btnPtzUpLeft = nullptr;
  QPushButton *btnPtzUpRight = nullptr;
  QPushButton *btnPtzDownLeft = nullptr;
  QPushButton *btnPtzDownRight = nullptr;
  QPushButton *btnPtzReset = nullptr;
  QPushButton *btnZoomIn = nullptr;
  QPushButton *btnZoomOut = nullptr;
};

struct TelegramUiRefs {
  QLabel *userCountLabel = nullptr;
  QLineEdit *entryPlateInput = nullptr;
  QPushButton *btnSendEntry = nullptr;
  QLineEdit *exitPlateInput = nullptr;
  QSpinBox *feeInput = nullptr;
  QPushButton *btnSendExit = nullptr;
  QTableWidget *userTable = nullptr;
};

struct DbUiRefs {
  QTabWidget *dbSubTabs = nullptr;
  QTableWidget *parkingLogTable = nullptr;
  QLineEdit *plateSearchInput = nullptr;
  QPushButton *btnSearchPlate = nullptr;
  QPushButton *btnRefreshLogs = nullptr;
  QLineEdit *forcePlateInput = nullptr;
  QSpinBox *forceObjectIdInput = nullptr;
  QPushButton *btnForcePlate = nullptr;
  QLineEdit *editPlateInput = nullptr;
  QPushButton *btnEditPlate = nullptr;
  QCheckBox *chkShowPlateLogs = nullptr;
  QTableWidget *reidTable = nullptr;
  QSpinBox *staleTimeoutInput = nullptr;
  QSpinBox *pruneTimeoutInput = nullptr;
  QCheckBox *chkShowStaleObjects = nullptr;
  QTableWidget *userDbTable = nullptr;
  QPushButton *btnRefreshUsers = nullptr;
  QPushButton *btnAddUser = nullptr;
  QPushButton *btnEditUser = nullptr;
  QPushButton *btnDeleteUser = nullptr;
  QTableWidget *vehicleTable = nullptr;
  QPushButton *btnRefreshVehicles = nullptr;
  QPushButton *btnDeleteVehicle = nullptr;
  QTableWidget *zoneTable = nullptr;
  QPushButton *btnRefreshZone = nullptr;
};

struct RecordUiRefs {
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
  QPushButton *btnVideoPlay = nullptr;
  QPushButton *btnVideoPause = nullptr;
  QPushButton *btnVideoStop = nullptr;
  QSlider *videoSeekSlider = nullptr;
  QLabel *videoTimeLabel = nullptr;
  QSpinBox *spinRecordRetention = nullptr;
  QLabel *lblContinuousStatus = nullptr;
  QPushButton *btnApplyContinuousSetting = nullptr;
  QPushButton *btnViewContinuous = nullptr;
};

struct MainWindowUiRefs : HeaderUiRefs,
                          SplashUiRefs,
                          CctvUiRefs,
                          TelegramUiRefs,
                          DbUiRefs,
                          RecordUiRefs {
  QTextEdit *logView = nullptr;
  QListWidget *eventListWidget = nullptr;
  QPushButton *btnPlay = nullptr;
  QStackedWidget *stackedWidget = nullptr;
};

#endif // MAINWINDOWUIREFS_H

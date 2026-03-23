#ifndef MAINWINDOWUIREFS_H
#define MAINWINDOWUIREFS_H

class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QToolButton;
class ToastOverlayWidget;
class VideoWidget;

struct HeaderUiRefs {
  QLabel *headerIconLabel = nullptr;
  QLabel *headerTitleLabel = nullptr;
  QToolButton *menuButton = nullptr;
  QMenu *navMenu = nullptr;
  QToolButton *settingsButton = nullptr;
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
  QCheckBox *chkPerson = nullptr;
  QCheckBox *chkFace = nullptr;
  QCheckBox *chkPlate = nullptr;
  QCheckBox *chkOther = nullptr;

  QCheckBox *chkShowFps = nullptr;
  QLabel *lblAvgFps = nullptr;

  QPushButton *btnCaptureManual = nullptr;
  QPushButton *btnRecordManual = nullptr;

  QLabel *footerTimeLabel = nullptr;
  QLabel *footerRecordingLabel = nullptr;
  QLabel *recordingDot = nullptr;
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

  QTableWidget *reidTable = nullptr;
  QSpinBox *staleTimeoutInput = nullptr;
  QSpinBox *pruneTimeoutInput = nullptr;
  QCheckBox *chkShowStaleObjects = nullptr;

  QTableWidget *userDbTable = nullptr;
  QPushButton *btnRefreshUsers = nullptr;
  QPushButton *btnAddUser = nullptr;
  QPushButton *btnEditUser = nullptr;
  QPushButton *btnDeleteUser = nullptr;

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
  QStackedWidget *stackedWidget = nullptr;
  ToastOverlayWidget *toastOverlay = nullptr;
};

#endif // MAINWINDOWUIREFS_H

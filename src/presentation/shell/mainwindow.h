#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "presentation/shell/mainwindowuirefs.h"
#include "presentation/widgets/videowidget.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

class MainWindowController;
class ControllerDialog;
class QGridLayout;
class QSplitter;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override = default;
  MainWindowUiRefs controllerUiRefs() const;
  void attachController(MainWindowController *controller);
  void showCctvSplash(const QString &message = QString());
  void showCctvPage();
  static constexpr int kDbPageIndex = 3;

protected:
  void closeEvent(QCloseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
#ifdef Q_OS_WIN
  bool nativeEvent(const QByteArray &eventType, void *message,
                   qintptr *result) override;
#endif

public slots:
  void navigateToPage(int stackedIndex);
  void navigateToDbSubTab(int tabIndex);

private:
  static constexpr int kSplashPageIndex = 0;
  static constexpr int kCctvPageIndex = 1;

  void setupUi();
  void openLogFilterSettings();

  VideoWidget *m_videoWidgets[4] = {nullptr, nullptr, nullptr, nullptr};
  QFrame *m_channelCards[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *m_channelStatusDots[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *m_channelNameLabels[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *m_thumbnailLabels[4] = {nullptr, nullptr, nullptr, nullptr};
  QGridLayout *m_videoGridLayout = nullptr;
  QComboBox *m_roiTargetCombo = nullptr;
  QLineEdit *m_roiNameEdit = nullptr;
  QComboBox *m_roiSelectorCombo = nullptr;
  QPushButton *m_btnExit = nullptr;
  QPushButton *m_btnApplyRoi = nullptr;
  QPushButton *m_btnFinishRoi = nullptr;
  QPushButton *m_btnDeleteRoi = nullptr;

  // Telegram Widgets
  QLabel *m_userCountLabel = nullptr;
  QLineEdit *m_entryPlateInput = nullptr;
  QPushButton *m_btnSendEntry = nullptr;
  QLineEdit *m_exitPlateInput = nullptr;
  QSpinBox *m_feeInput = nullptr;
  QPushButton *m_btnSendExit = nullptr;
  QTableWidget *m_userTable = nullptr;

  // Parking DB Panel Widgets
  QTableWidget *m_parkingLogTable = nullptr;
  QLineEdit *m_plateSearchInput = nullptr;
  QPushButton *m_btnSearchPlate = nullptr;
  QPushButton *m_btnRefreshLogs = nullptr;
  QLineEdit *m_forcePlateInput = nullptr;
  QSpinBox *m_forceObjectIdInput = nullptr;
  QPushButton *m_btnForcePlate = nullptr;
  QLineEdit *m_editPlateInput = nullptr;
  QPushButton *m_btnEditPlate = nullptr;

  // New DB sub-tab widgets
  QTableWidget *m_userDbTable = nullptr;
  QPushButton *m_btnRefreshUsers = nullptr;
  QPushButton *m_btnAddUser = nullptr;
  QPushButton *m_btnEditUser = nullptr;
  QPushButton *m_btnDeleteUser = nullptr;

  QTableWidget *m_zoneTable = nullptr;
  QPushButton *m_btnRefreshZone = nullptr;
  QTabWidget *m_dbSubTabs = nullptr;

  // Object Type Filter Checkboxes
  QCheckBox *m_chkVehicle = nullptr;
  QCheckBox *m_chkPerson = nullptr;
  QCheckBox *m_chkFace = nullptr;
  QCheckBox *m_chkPlate = nullptr;
  QCheckBox *m_chkOther = nullptr;

  // Log Filter
  QCheckBox *m_chkShowFps = nullptr;
  QLabel *m_lblAvgFps = nullptr;

  QTableWidget *m_reidTable = nullptr;
  QSpinBox *m_staleTimeoutInput = nullptr;
  QSpinBox *m_pruneTimeoutInput = nullptr;
  QCheckBox *m_chkShowStaleObjects = nullptr;

  // Dashboard Header/Footer/Event Panel
  QLabel *m_headerTitleLabel = nullptr;
  QLabel *m_footerTimeLabel = nullptr;
  QLabel *m_footerRecordingLabel = nullptr;
  QLabel *m_recordingDot = nullptr;

  QPushButton *m_btnCaptureManual = nullptr;
  QPushButton *m_btnRecordManual = nullptr;

  // Recording Search Widgets
  QTableWidget *m_recordLogTable = nullptr;
  QPushButton *m_btnRefreshRecordLogs = nullptr;
  QPushButton *m_btnDeleteRecordLog = nullptr;
  VideoWidget *m_recordVideoWidget = nullptr;
  // Recording search: extra test controls
  QLineEdit *m_recordEventTypeInput = nullptr;
  QSpinBox *m_recordIntervalSpin = nullptr;
  QPushButton *m_btnApplyEventSetting = nullptr;
  QPushButton *m_btnTriggerEventRecord = nullptr;
  QComboBox *m_cmbManualCamera = nullptr;
  QPushButton *m_btnCaptureRecordTab = nullptr;
  QPushButton *m_btnRecordRecordTab = nullptr;
  QLabel *m_recordPreviewPathLabel = nullptr;

  // Video player controls
  QPushButton *m_btnVideoPlay = nullptr;
  QPushButton *m_btnVideoPause = nullptr;
  QPushButton *m_btnVideoStop = nullptr;
  QSlider *m_videoSeekSlider = nullptr;
  QLabel *m_videoTimeLabel = nullptr;

  // Continuous Recording (상시 녹화)
  QSpinBox *m_spinRecordRetention = nullptr;
  QLabel *m_lblContinuousStatus = nullptr;
  QPushButton *m_btnApplyContinuousSetting = nullptr;
  QPushButton *m_btnViewContinuous = nullptr;

  QTimer *m_clockTimer = nullptr;
  QToolButton *m_menuButton = nullptr;
  QMenu *m_navMenu = nullptr;
  QToolButton *m_settingsButton = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QLabel *m_splashTitleLabel = nullptr;
  QLabel *m_splashMessageLabel = nullptr;
  bool m_isCctvReady = false;
  QPoint m_dragPosition;

  MainWindowController *m_controller = nullptr;
  ControllerDialog *m_controllerDialog = nullptr;
};

#endif // MAINWINDOW_H

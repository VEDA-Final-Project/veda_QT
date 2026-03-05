#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "mainwindowuirefs.h"
#include "ui/video/videowidget.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>

class MainWindowController;
class QSplitter;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override = default;
  MainWindowUiRefs controllerUiRefs() const;
  void attachController(MainWindowController *controller);

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

private:
  void setupUi();

  QSplitter *m_videoSplitter = nullptr;
  VideoWidget *m_videoWidgetPrimary = nullptr;
  VideoWidget *m_videoWidgetSecondary = nullptr;
  QFrame *m_channelCards[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *m_channelStatusDots[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *m_channelNameLabels[4] = {nullptr, nullptr, nullptr, nullptr};
  QLabel *m_thumbnailLabels[4] = {nullptr, nullptr, nullptr, nullptr};
  int m_selectedChannelIndex = 0;
  QComboBox *m_roiTargetCombo = nullptr;
  QLineEdit *m_roiNameEdit = nullptr;
  QComboBox *m_roiSelectorCombo = nullptr;
  QTextEdit *m_logView = nullptr;
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

  // RPi Widgets
  QLineEdit *m_rpiHostEdit = nullptr;
  QSpinBox *m_rpiPortSpin = nullptr;
  QPushButton *m_btnRpiConnect = nullptr;
  QPushButton *m_btnRpiDisconnect = nullptr;
  QPushButton *m_btnBarrierUp = nullptr;
  QPushButton *m_btnBarrierDown = nullptr;
  QPushButton *m_btnLedOn = nullptr;
  QPushButton *m_btnLedOff = nullptr;
  QLabel *m_rpiConnectionStatusLabel = nullptr;
  QLabel *m_rpiVehicleStatusLabel = nullptr;
  QLabel *m_rpiLedStatusLabel = nullptr;
  QLabel *m_rpiIrRawLabel = nullptr;
  QLabel *m_rpiServoAngleLabel = nullptr;

  // Parking DB Panel Widgets
  QTableWidget *m_parkingLogTable = nullptr;
  QLineEdit *m_plateSearchInput = nullptr;
  QPushButton *m_btnSearchPlate = nullptr;
  QPushButton *m_btnRefreshLogs = nullptr;
  QLineEdit *m_forcePlateInput = nullptr;
  QSpinBox *m_forceObjectIdInput = nullptr;
  QLineEdit *m_forceTypeInput = nullptr;
  QDoubleSpinBox *m_forceScoreInput = nullptr;
  QLineEdit *m_forceBBoxInput = nullptr;
  QPushButton *m_btnForcePlate = nullptr;
  QLineEdit *m_editPlateInput = nullptr;
  QPushButton *m_btnEditPlate = nullptr;

  // New DB sub-tab widgets
  QTableWidget *m_userDbTable = nullptr;
  QPushButton *m_btnRefreshUsers = nullptr;
  QPushButton *m_btnDeleteUser = nullptr;

  QTableWidget *m_hwLogTable = nullptr;
  QPushButton *m_btnRefreshHwLogs = nullptr;
  QPushButton *m_btnClearHwLogs = nullptr;

  QTableWidget *m_vehicleTable = nullptr;
  QPushButton *m_btnRefreshVehicles = nullptr;
  QPushButton *m_btnDeleteVehicle = nullptr;

  QTableWidget *m_zoneTable = nullptr;
  QPushButton *m_btnRefreshZone = nullptr;

  // Object Type Filter Checkboxes
  QCheckBox *m_chkVehicle = nullptr;
  QCheckBox *m_chkPerson = nullptr;
  QCheckBox *m_chkFace = nullptr;
  QCheckBox *m_chkPlate = nullptr;
  QCheckBox *m_chkOther = nullptr;

  // Log Filter
  QCheckBox *m_chkShowPlateLogs = nullptr;
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
  QListWidget *m_eventListWidget = nullptr;
  QTimer *m_clockTimer = nullptr;
  QToolButton *m_menuButton = nullptr;
  QMenu *m_navMenu = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QPoint m_dragPosition;

  MainWindowController *m_controller = nullptr;
};

#endif // MAINWINDOW_H

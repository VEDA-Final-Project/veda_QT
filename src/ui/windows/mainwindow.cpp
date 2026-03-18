#include "mainwindow.h"
#include "config/logfilterconfig.h"
#include "mainwindowcontroller.h"
#include <QDialog>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

// 아이콘 틴팅(색상 변경)을 위한 헬퍼 함수
static QIcon tintIcon(const QString &path, const QColor &color) {
  QPixmap pixmap(path);
  if (pixmap.isNull())
    return QIcon();

  QPixmap result(pixmap.size());
  result.fill(Qt::transparent);

  QPainter painter(&result);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.drawPixmap(0, 0, pixmap);
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(result.rect(), color);
  painter.end();

  return QIcon(result);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  // 프레임리스 윈도우 (타이틀바 제거)
  setWindowFlags(Qt::FramelessWindowHint);

  // UI 레이아웃 및 위젯 생성
  setupUi();

  // 종료 버튼이 존재하면 클릭 시 창 닫기
  if (m_btnExit) {
    connect(m_btnExit, &QPushButton::clicked, this, &MainWindow::close);
  }

  // 실시간 시계 타이머
  m_clockTimer = new QTimer(this);
  connect(m_clockTimer, &QTimer::timeout, this, [this]() {
    if (m_footerTimeLabel) {
      QDateTime now = QDateTime::currentDateTime();
      m_footerTimeLabel->setText(now.toString("yyyy/MM/dd  HH:mm:ss"));
    }
  });
  m_clockTimer->start(1000);
  // 즉시 한 번 갱신
  if (m_footerTimeLabel) {
    m_footerTimeLabel->setText(
        QDateTime::currentDateTime().toString("yyyy/MM/dd  HH:mm:ss"));
  }

  // 초기 창 크기 설정
  resize(1280, 720);
}

MainWindowUiRefs MainWindow::controllerUiRefs() const {
  MainWindowUiRefs uiRefs;
  for (int i = 0; i < 4; ++i) {
    uiRefs.videoWidgets[i] = m_videoWidgets[i];
    uiRefs.channelCards[i] = m_channelCards[i];
    uiRefs.channelStatusDots[i] = m_channelStatusDots[i];
    uiRefs.channelNameLabels[i] = m_channelNameLabels[i];
  }
  uiRefs.roiTargetCombo = m_roiTargetCombo;
  uiRefs.roiNameEdit = m_roiNameEdit;
  uiRefs.roiSelectorCombo = m_roiSelectorCombo;
  uiRefs.logView = m_logView;
  for (int i = 0; i < 4; ++i) {
    uiRefs.thumbnailLabels[i] = m_thumbnailLabels[i];
  }
  uiRefs.videoGridLayout = m_videoGridLayout;
  uiRefs.btnApplyRoi = m_btnApplyRoi;
  uiRefs.btnFinishRoi = m_btnFinishRoi;
  uiRefs.btnDeleteRoi = m_btnDeleteRoi;

  uiRefs.userCountLabel = m_userCountLabel;
  uiRefs.entryPlateInput = m_entryPlateInput;
  uiRefs.btnSendEntry = m_btnSendEntry;
  uiRefs.exitPlateInput = m_exitPlateInput;
  uiRefs.feeInput = m_feeInput;
  uiRefs.btnSendExit = m_btnSendExit;
  uiRefs.userTable = m_userTable;

  uiRefs.parkingLogTable = m_parkingLogTable;
  uiRefs.plateSearchInput = m_plateSearchInput;
  uiRefs.btnSearchPlate = m_btnSearchPlate;
  uiRefs.btnRefreshLogs = m_btnRefreshLogs;
  uiRefs.forcePlateInput = m_forcePlateInput;
  uiRefs.forceObjectIdInput = m_forceObjectIdInput;
  uiRefs.btnForcePlate = m_btnForcePlate;
  uiRefs.editPlateInput = m_editPlateInput;
  uiRefs.btnEditPlate = m_btnEditPlate;
  uiRefs.chkShowPlateLogs = m_chkShowPlateLogs;
  uiRefs.chkShowFps = m_chkShowFps;
  uiRefs.lblAvgFps = m_lblAvgFps;
  uiRefs.reidTable = m_reidTable;
  uiRefs.staleTimeoutInput = m_staleTimeoutInput;
  uiRefs.pruneTimeoutInput = m_pruneTimeoutInput;
  uiRefs.chkShowStaleObjects = m_chkShowStaleObjects;

  uiRefs.userDbTable = m_userDbTable;
  uiRefs.btnRefreshUsers = m_btnRefreshUsers;
  uiRefs.btnAddUser = m_btnAddUser;
  uiRefs.btnEditUser = m_btnEditUser;
  uiRefs.btnDeleteUser = m_btnDeleteUser;
  uiRefs.vehicleTable = m_vehicleTable;
  uiRefs.btnRefreshVehicles = m_btnRefreshVehicles;
  uiRefs.btnDeleteVehicle = m_btnDeleteVehicle;
  uiRefs.zoneTable = m_zoneTable;
  uiRefs.btnRefreshZone = m_btnRefreshZone;
  uiRefs.eventListWidget = m_eventListWidget;

  uiRefs.recordLogTable = m_recordLogTable;
  uiRefs.btnRefreshRecordLogs = m_btnRefreshRecordLogs;
  uiRefs.btnDeleteRecordLog = m_btnDeleteRecordLog;
  uiRefs.recordVideoWidget = m_recordVideoWidget;
  uiRefs.recordEventTypeInput = m_recordEventTypeInput;
  uiRefs.recordIntervalSpin = m_recordIntervalSpin;
  uiRefs.btnApplyEventSetting = m_btnApplyEventSetting;
  uiRefs.btnTriggerEventRecord = m_btnTriggerEventRecord;
  uiRefs.recordPreviewPathLabel = m_recordPreviewPathLabel;
  uiRefs.cmbManualCamera = m_cmbManualCamera;
  uiRefs.btnCaptureRecordTab = m_btnCaptureRecordTab;
  uiRefs.btnRecordRecordTab = m_btnRecordRecordTab;
  uiRefs.btnVideoPlay = m_btnVideoPlay;
  uiRefs.btnVideoPause = m_btnVideoPause;
  uiRefs.btnVideoStop = m_btnVideoStop;
  uiRefs.videoSeekSlider = m_videoSeekSlider;
  uiRefs.videoTimeLabel = m_videoTimeLabel;

  uiRefs.spinRecordRetention = m_spinRecordRetention;
  uiRefs.lblContinuousStatus = m_lblContinuousStatus;
  uiRefs.btnApplyContinuousSetting = m_btnApplyContinuousSetting;
  uiRefs.btnViewContinuous = m_btnViewContinuous;

  uiRefs.btnCaptureManual = m_btnCaptureManual;
  uiRefs.btnRecordManual = m_btnRecordManual;

  return uiRefs;
}

void MainWindow::attachController(MainWindowController *controller) {
  m_controller = controller;
  if (!m_controller) {
    return;
  }

  auto updateFilter = [this]() {
    if (!m_controller)
      return;
    QSet<QString> disabled;
    if (!m_chkVehicle->isChecked()) {
      // 한화 카메라는 차종별로 타입을 보냄 (Vehical은 펌웨어 오타)
      disabled.insert("Vehicle");
      disabled.insert("Vehical");
      disabled.insert("Car");
      disabled.insert("Bus");
      disabled.insert("Truck");
      disabled.insert("Motorcycle");
      disabled.insert("Bicycle");
    }
    if (!m_chkPerson->isChecked())
      disabled.insert("Human");
    if (!m_chkFace->isChecked())
      disabled.insert("Face");
    if (!m_chkPlate->isChecked())
      disabled.insert("LicensePlate");
    if (!m_chkOther->isChecked())
      disabled.insert("Other");
    m_controller->updateObjectFilter(disabled);
  };
  connect(m_chkVehicle, &QCheckBox::toggled, this, updateFilter);
  connect(m_chkPerson, &QCheckBox::toggled, this, updateFilter);
  connect(m_chkFace, &QCheckBox::toggled, this, updateFilter);
  connect(m_chkPlate, &QCheckBox::toggled, this, updateFilter);
  connect(m_chkOther, &QCheckBox::toggled, this, updateFilter);
  updateFilter();
}

/*
 * 창이 닫힐 때 호출되는 이벤트
 * - Controller의 종료 로직(shutdown) 수행
 * - 스레드 / RTSP / 리소스 정리 목적
 */
void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_controller) {
    m_controller->shutdown();
  }
  QMainWindow::closeEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && event->position().y() < 52) {
    m_dragPosition =
        event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
  }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
  if (event->buttons() & Qt::LeftButton && !m_dragPosition.isNull()) {
    move(event->globalPosition().toPoint() - m_dragPosition);
    event->accept();
  }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
  m_dragPosition = QPoint();
  QMainWindow::mouseReleaseEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::MouseButtonPress) {
    if (watched == m_headerTitleLabel ||
        (watched->objectName() == "headerIcon")) {
      if (m_stackedWidget) {
        m_stackedWidget->setCurrentIndex(m_isCctvReady ? kCctvPageIndex
                                                       : kSplashPageIndex);
      }
      return true;
    }
  }

  // 사이드바 위젯들이 휠 이벤트를 가로채서 스크롤이 끊기는 문제 해결
  if (event->type() == QEvent::Wheel) {
    if (qobject_cast<QComboBox *>(watched) ||
        qobject_cast<QAbstractSpinBox *>(watched) ||
        qobject_cast<QLineEdit *>(watched)) {
      // 이벤트를 가로채서 부모 위젯(ScrollArea의 콘텐츠)으로 전달하여 스크롤
      // 유도
      if (watched->parent()) {
        QCoreApplication::sendEvent(watched->parent(), event);
      }
      return true; // 원본 위젯의 기본 동작(값 변경 등)은 막음
    }
  }

  return QMainWindow::eventFilter(watched, event);
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
                             qintptr *result) {
  constexpr int RESIZE_BORDER = 6; // px
  auto *msg = static_cast<MSG *>(message);
  if (msg->message == WM_NCHITTEST) {
    const LONG x = GET_X_LPARAM(msg->lParam);
    const LONG y = GET_Y_LPARAM(msg->lParam);
    RECT rc;
    GetWindowRect(reinterpret_cast<HWND>(winId()), &rc);

    const bool left = (x >= rc.left && x < rc.left + RESIZE_BORDER);
    const bool right = (x <= rc.right && x > rc.right - RESIZE_BORDER);
    const bool top = (y >= rc.top && y < rc.top + RESIZE_BORDER);
    const bool bottom = (y <= rc.bottom && y > rc.bottom - RESIZE_BORDER);

    if (top && left) {
      *result = HTTOPLEFT;
      return true;
    }
    if (top && right) {
      *result = HTTOPRIGHT;
      return true;
    }
    if (bottom && left) {
      *result = HTBOTTOMLEFT;
      return true;
    }
    if (bottom && right) {
      *result = HTBOTTOMRIGHT;
      return true;
    }
    if (left) {
      *result = HTLEFT;
      return true;
    }
    if (right) {
      *result = HTRIGHT;
      return true;
    }
    if (top) {
      *result = HTTOP;
      return true;
    }
    if (bottom) {
      *result = HTBOTTOM;
      return true;
    }
  }
  return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::setupUi() {
  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  // 기본 UI 폰트를 한 단계만 줄여 과도하게 크게 보이는 문제를 완화.
  QFont compactUiFont = font();
  int basePointSize = compactUiFont.pointSize();
  if (basePointSize <= 0) {
    basePointSize = 10;
  }
  compactUiFont.setPointSize(basePointSize - 1);
  if (compactUiFont.pointSize() < 9) {
    compactUiFont.setPointSize(9);
  }
  centralWidget->setFont(compactUiFont);

  // 컨트롤러나 내부 로직에서 필요하지만 UI상으로는 보이지 않아야 할 위젯들을
  // 모아두는 컨테이너
  QWidget *hiddenContainer = new QWidget(this);
  hiddenContainer->setVisible(false);

  QVBoxLayout *layout = new QVBoxLayout(centralWidget);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // ── App-level Header / Toolbar ──
  QFrame *headerFrame = new QFrame(this);
  headerFrame->setObjectName("headerFrame");
  headerFrame->setFixedHeight(52);
  QHBoxLayout *headerLayout = new QHBoxLayout(headerFrame);
  headerLayout->setContentsMargins(12, 6, 12, 6);
  headerLayout->setSpacing(0);

  QLabel *headerIcon = new QLabel(this);
  headerIcon->setObjectName("headerIcon");
  headerIcon->setText(QString::fromUtf8("\xF0\x9F\x93\xA1"));
  headerIcon->setFixedSize(32, 32);
  headerIcon->setAlignment(Qt::AlignCenter);

  m_headerTitleLabel = new QLabel("Veda CCTV Dashboard", this);
  m_headerTitleLabel->setObjectName("headerTitle");

  headerLayout->addWidget(headerIcon);
  headerLayout->addSpacing(8);
  headerLayout->addWidget(m_headerTitleLabel);
  headerLayout->addSpacing(24);

  // Navigation: header icon+title clickable to go home + hamburger menu
  QStackedWidget *stackedWidget = new QStackedWidget(this);

  // Make icon+title clickable -> return to CCTV (home) page
  headerIcon->setCursor(Qt::PointingHandCursor);
  m_headerTitleLabel->setCursor(Qt::PointingHandCursor);
  headerIcon->installEventFilter(this);
  m_headerTitleLabel->installEventFilter(this);

  // Hamburger menu button
  m_menuButton = new QToolButton(this);
  m_menuButton->setIcon(
      tintIcon(PROJECT_SOURCE_DIR "/src/ui/icon/menu.png", QColor("#94A3B8")));
  m_menuButton->setIconSize(QSize(18, 18));
  m_menuButton->setObjectName("navBtn");
  m_menuButton->setPopupMode(QToolButton::InstantPopup);
  m_menuButton->setCursor(Qt::PointingHandCursor);

  m_navMenu = new QMenu(this);
  m_navMenu->setObjectName("navMenu");
  const QStringList menuLabels = {
      QString::fromUtf8("\xF0\x9F\x93\xB1 "
                        "\xED\x85\x94\xEB\xA0\x88\xEA\xB7\xB8\xEB\x9E\xA8"),
      QString::fromUtf8("\xF0\x9F\x97\x84\xEF\xB8\x8F DB"),
      QString::fromUtf8("\xF0\x9F\x93\xBD REC")};
  const QList<int> menuIndices = {2, 3, 4};

  for (int i = 0; i < menuLabels.size(); ++i) {
    QAction *action = m_navMenu->addAction(menuLabels[i]);
    connect(action, &QAction::triggered, this,
            [=]() { stackedWidget->setCurrentIndex(menuIndices[i]); });
  }

  m_menuButton->setMenu(m_navMenu);
  headerLayout->addWidget(m_menuButton);
  headerLayout->addSpacing(2);

  // Settings (gear) button — 로그 필터 설정
  m_settingsButton = new QToolButton(this);
  m_settingsButton->setIcon(tintIcon(
      PROJECT_SOURCE_DIR "/src/ui/icon/settings.png", QColor("#94A3B8")));
  m_settingsButton->setIconSize(QSize(18, 18));
  m_settingsButton->setObjectName("navBtn");
  m_settingsButton->setCursor(Qt::PointingHandCursor);
  m_settingsButton->setToolTip(
      QString::fromUtf8("\xEB\xA1\x9C\xEA\xB7\xB8 \xEC\x84\xA4\xEC\xA0\x95"));
  connect(m_settingsButton, &QToolButton::clicked, this,
          &MainWindow::openLogFilterSettings);
  headerLayout->addWidget(m_settingsButton);
  headerLayout->addSpacing(2);

  // Store stackedWidget for eventFilter usage
  m_stackedWidget = stackedWidget;

  headerLayout->addStretch();

  // ── Window Control Buttons: Minimize, Maximize/Restore, Close ──
  QPushButton *btnMinimize = new QPushButton(this);
  btnMinimize->setIcon(tintIcon(PROJECT_SOURCE_DIR "/src/ui/icon/minimize.png",
                                QColor("#94A3B8")));
  btnMinimize->setIconSize(QSize(18, 18));
  btnMinimize->setObjectName("btnWindowCtrl");
  btnMinimize->setFixedSize(32, 32);
  btnMinimize->setToolTip(
      QString::fromUtf8("\xEC\xB5\x9C\xEC\x86\x8C\xED\x99\x94"));
  connect(btnMinimize, &QPushButton::clicked, this, &MainWindow::showMinimized);
  headerLayout->addWidget(btnMinimize);

  QPushButton *btnMaxRestore = new QPushButton(this);
  btnMaxRestore->setIcon(tintIcon(
      PROJECT_SOURCE_DIR "/src/ui/icon/maximize.png", QColor("#94A3B8")));
  btnMaxRestore->setIconSize(QSize(18, 18));
  btnMaxRestore->setObjectName("btnWindowCtrl");
  btnMaxRestore->setFixedSize(32, 32);
  btnMaxRestore->setToolTip(
      QString::fromUtf8("\xEC\xB5\x9C\xEB\x8C\x80\xED\x99\x94"));
  connect(btnMaxRestore, &QPushButton::clicked, this, [=]() {
    if (isMaximized()) {
      showNormal();
      btnMaxRestore->setToolTip(
          QString::fromUtf8("\xEC\xB5\x9C\xEB\x8C\x80\xED\x99\x94"));
    } else {
      showMaximized();
      btnMaxRestore->setToolTip(QString::fromUtf8(
          "\xEC\x9B\x90\xEB\x9E\x98 \xED\x81\xAC\xEA\xB8\xB0"));
    }
  });
  headerLayout->addWidget(btnMaxRestore);

  m_btnExit = new QPushButton(this);
  m_btnExit->setIcon(
      tintIcon(PROJECT_SOURCE_DIR "/src/ui/icon/exit.png", QColor("#94A3B8")));
  m_btnExit->setIconSize(QSize(18, 18));
  m_btnExit->setObjectName("btnClose");
  m_btnExit->setFixedSize(32, 32);
  m_btnExit->setToolTip(QString::fromUtf8("\xEC\xA2\x85\xEB\xA3\x8C"));
  headerLayout->addWidget(m_btnExit);

  layout->addWidget(headerFrame);

  // ======================
  // Page 0: CCTV Splash
  // ======================
  QWidget *splashTab = new QWidget(this);
  splashTab->setObjectName("cctvSplashPage");
  QVBoxLayout *splashLayout = new QVBoxLayout(splashTab);
  splashLayout->setContentsMargins(24, 24, 24, 24);
  splashLayout->setSpacing(18);
  splashLayout->addStretch();

  QFrame *splashCard = new QFrame(splashTab);
  splashCard->setObjectName("cctvSplashCard");
  splashCard->setMaximumWidth(560);
  QVBoxLayout *splashCardLayout = new QVBoxLayout(splashCard);
  splashCardLayout->setContentsMargins(36, 30, 36, 30);
  splashCardLayout->setSpacing(14);

  m_splashTitleLabel =
      new QLabel(QString::fromUtf8("CCTV 준비 중"), splashCard);
  m_splashTitleLabel->setObjectName("cctvSplashTitle");
  m_splashTitleLabel->setAlignment(Qt::AlignCenter);

  m_splashMessageLabel = new QLabel(
      QString::fromUtf8("카메라 연결을 확인하고 있습니다."), splashCard);
  m_splashMessageLabel->setObjectName("cctvSplashMessage");
  m_splashMessageLabel->setWordWrap(true);
  m_splashMessageLabel->setAlignment(Qt::AlignCenter);

  QProgressBar *splashProgress = new QProgressBar(splashCard);
  splashProgress->setObjectName("cctvSplashProgress");
  splashProgress->setRange(0, 0);
  splashProgress->setTextVisible(false);
  splashProgress->setFixedHeight(8);

  splashCardLayout->addWidget(m_splashTitleLabel);
  splashCardLayout->addWidget(m_splashMessageLabel);
  splashCardLayout->addWidget(splashProgress);
  splashLayout->addWidget(splashCard, 0, Qt::AlignHCenter);
  splashLayout->addStretch();

  // ======================
  // Page 1: CCTV 보기 (Dashboard 3-Panel Layout)
  // ======================
  QWidget *cctvTab = new QWidget(this);
  QVBoxLayout *cctvLayout = new QVBoxLayout(cctvTab);
  cctvLayout->setContentsMargins(0, 0, 0, 0);
  cctvLayout->setSpacing(0);

  // ── Main 3-Panel Area (Resizable via Splitter) ──
  QSplitter *mainSplitter = new QSplitter(Qt::Horizontal, this);
  mainSplitter->setHandleWidth(4);
  mainSplitter->setChildrenCollapsible(false);

  // ── Left Panel: Channel Controls (Scroll Area 적용 — 기존 테마 색상 유지) ──
  QScrollArea *channelScrollArea = new QScrollArea(this);
  channelScrollArea->setObjectName("channelScrollArea");
  channelScrollArea->setWidgetResizable(true);
  channelScrollArea->setMaximumWidth(400);
  channelScrollArea->setFrameShape(QFrame::NoFrame);
  channelScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  channelScrollArea->setStyleSheet(
      "QScrollArea { background: transparent; border: none; }");

  QFrame *channelPanel = new QFrame(channelScrollArea);
  channelPanel->setObjectName("channelPanel");
  channelPanel->setMinimumWidth(0); // 접기 허용
  QVBoxLayout *channelPanelLayout = new QVBoxLayout(channelPanel);
  channelPanelLayout->setContentsMargins(12, 12, 4, 12);
  channelPanelLayout->setSpacing(8);

  QLabel *channelTitle = new QLabel(QString::fromUtf8("CHANNELS"), this);
  channelTitle->setObjectName("panelTitle");
  channelPanelLayout->addWidget(channelTitle);
  channelPanelLayout->addSpacing(4);

  // 채널 카드 (클릭으로 전환)
  for (int i = 0; i < 4; ++i) {
    QFrame *card = new QFrame(this);
    card->setObjectName(QString("channelCard%1").arg(i));
    card->setProperty("selected", i == 0);
    card->setFixedHeight(90);
    card->setCursor(Qt::PointingHandCursor);
    card->setStyleSheet("QFrame { background: #1a1a2e; border: 2px solid #333; "
                        "border-radius: 8px; }"
                        "QFrame[selected=\"true\"] { border-color: #00e676; }");

    QHBoxLayout *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(10, 8, 10, 8);

    // 썸네일 노출용 QLabel
    QLabel *thumbnailLabel = new QLabel(card);
    thumbnailLabel->setFixedSize(120, 68);
    thumbnailLabel->setStyleSheet("background: #000; border-radius: 4px;");
    thumbnailLabel->setAlignment(Qt::AlignCenter);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *nameLabel = new QLabel(QString("Ch%1").arg(i + 1), card);
    nameLabel->setStyleSheet("color: #ccc; font-size: 13px; font-weight: bold; "
                             "background: transparent; border: none;");

    QLabel *statusDot = new QLabel(card);
    statusDot->setFixedSize(10, 10);
    statusDot->setStyleSheet(
        "background: #10b981; border-radius: 5px; border: none;");

    infoLayout->addWidget(nameLabel);
    infoLayout->addStretch();
    infoLayout->addWidget(statusDot);

    cardLayout->addWidget(thumbnailLabel);
    cardLayout->addSpacing(12);
    cardLayout->addLayout(infoLayout, 1);

    m_channelCards[i] = card;
    m_channelNameLabels[i] = nameLabel;
    m_channelStatusDots[i] = statusDot;
    m_thumbnailLabels[i] = thumbnailLabel;
    channelPanelLayout->addWidget(card);
  }
  channelPanelLayout->addSpacing(12);

  // ── ROI Controls ──
  QLabel *roiTitle = new QLabel(QString::fromUtf8("ROI 설정"), this);
  roiTitle->setObjectName("panelTitle");
  channelPanelLayout->addWidget(roiTitle);
  channelPanelLayout->addSpacing(4);

  // ROI 대상
  QLabel *roiTargetLabel = new QLabel(QString::fromUtf8("ROI 대상"), this);
  m_roiTargetCombo = new QComboBox(this);
  m_roiTargetCombo->addItem(QStringLiteral("Ch1"));
  m_roiTargetCombo->addItem(QStringLiteral("Ch2"));
  m_roiTargetCombo->addItem(QStringLiteral("Ch3"));
  m_roiTargetCombo->addItem(QStringLiteral("Ch4"));
  m_roiTargetCombo->installEventFilter(this);
  channelPanelLayout->addWidget(roiTargetLabel);
  channelPanelLayout->addWidget(m_roiTargetCombo);

  // ROI 이름
  QLabel *nameLabel = new QLabel(QString::fromUtf8("이름"), this);
  m_roiNameEdit = new QLineEdit(this);
  m_roiNameEdit->setPlaceholderText(QStringLiteral("ROI 이름 입력(필수)"));
  m_roiNameEdit->setClearButtonEnabled(true);
  m_roiNameEdit->setMaxLength(20);
  m_roiNameEdit->setValidator(new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("^[A-Za-z0-9가-힣 _-]{0,20}$")),
      m_roiNameEdit));
  m_roiNameEdit->installEventFilter(this);
  channelPanelLayout->addWidget(nameLabel);
  channelPanelLayout->addWidget(m_roiNameEdit);

  // ROI 선택
  QLabel *roiLabel = new QLabel("ROI", this);
  m_roiSelectorCombo = new QComboBox(this);
  m_roiSelectorCombo->setMinimumContentsLength(12);
  m_roiSelectorCombo->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);
  m_roiSelectorCombo->installEventFilter(this);
  channelPanelLayout->addWidget(roiLabel);
  channelPanelLayout->addWidget(m_roiSelectorCombo);
  channelPanelLayout->addSpacing(4);

  // ROI 버튼들
  m_btnApplyRoi = new QPushButton(QString::fromUtf8("구역 설정"), this);
  m_btnFinishRoi = new QPushButton(QString::fromUtf8("ROI 완료"), this);
  m_btnDeleteRoi = new QPushButton(QString::fromUtf8("ROI 삭제"), this);
  channelPanelLayout->addWidget(m_btnApplyRoi);
  channelPanelLayout->addWidget(m_btnFinishRoi);
  channelPanelLayout->addWidget(m_btnDeleteRoi);
  channelPanelLayout->addSpacing(12);

  // ── 객체 감지 필터 ──
  QLabel *filterTitle = new QLabel(QString::fromUtf8("OBJECT FILTER"), this);
  filterTitle->setObjectName("panelTitle");
  channelPanelLayout->addWidget(filterTitle);
  channelPanelLayout->addSpacing(4);

  m_chkVehicle = new QCheckBox(QString::fromUtf8("차량"), this);
  m_chkVehicle->setChecked(true);
  m_chkPerson = new QCheckBox(QString::fromUtf8("사람"), this);
  m_chkPerson->setChecked(true);
  m_chkFace = new QCheckBox(QString::fromUtf8("얼굴"), this);
  m_chkFace->setChecked(true);
  m_chkPlate = new QCheckBox(QString::fromUtf8("번호판"), this);
  m_chkPlate->setChecked(true);
  m_chkOther = new QCheckBox(QString::fromUtf8("기타"), this);
  m_chkOther->setChecked(false);

  channelPanelLayout->addWidget(m_chkVehicle);
  channelPanelLayout->addWidget(m_chkPerson);
  channelPanelLayout->addWidget(m_chkFace);
  channelPanelLayout->addWidget(m_chkPlate);
  channelPanelLayout->addWidget(m_chkOther);
  channelPanelLayout->addSpacing(12);

  // ── 디스플레이 설정 ──
  QLabel *displayTitle = new QLabel(QString::fromUtf8("DISPLAY"), this);
  displayTitle->setObjectName("panelTitle");
  channelPanelLayout->addWidget(displayTitle);
  channelPanelLayout->addSpacing(4);

  m_chkShowFps = new QCheckBox(QString::fromUtf8("FPS 표시"), this);
  m_lblAvgFps = new QLabel(QString::fromUtf8("최근 1분 평균 FPS: 0.0"), this);
  channelPanelLayout->addWidget(m_chkShowFps);
  channelPanelLayout->addWidget(m_lblAvgFps);
  channelPanelLayout->addSpacing(12);

  // ── 미디어 제어 ──
  QLabel *mediaTitle = new QLabel(QString::fromUtf8("MEDIA"), this);
  mediaTitle->setObjectName("panelTitle");
  channelPanelLayout->addWidget(mediaTitle);
  channelPanelLayout->addSpacing(4);

  m_btnCaptureManual = new QPushButton(QString::fromUtf8("이미지 캡처"), this);
  m_btnRecordManual = new QPushButton(QString::fromUtf8("영상 녹화"), this);
  m_btnRecordManual->setCheckable(true); // 토글 버튼으로 사용
  channelPanelLayout->addWidget(m_btnCaptureManual);
  channelPanelLayout->addWidget(m_btnRecordManual);

  channelPanelLayout->addStretch();
  channelScrollArea->setWidget(channelPanel);

  // ── Center Panel: Video Feeds ──
  QWidget *centerPanel = new QWidget(this);
  QVBoxLayout *centerLayout = new QVBoxLayout(centerPanel);
  centerLayout->setContentsMargins(0, 0, 0, 0); // 공백 완전히 제거
  centerLayout->setSpacing(4);

  // 상단 토글 버튼 바 (좌우 패널 접기 제어 - 각 패널 상단 구석에 완벽히 밀착)
  QHBoxLayout *toggleBarLayout = new QHBoxLayout();
  toggleBarLayout->setContentsMargins(0, 0, 0, 0);
  toggleBarLayout->setSpacing(0);

  QPushButton *btnToggleChannel =
      new QPushButton(QString::fromUtf8("\xE2\x97\x80"), this);
  btnToggleChannel->setObjectName("btnToggleSidebar");
  btnToggleChannel->setFixedSize(24, 24);
  btnToggleChannel->setCursor(Qt::PointingHandCursor);
  btnToggleChannel->setToolTip(QString::fromUtf8("채널 패널 보이기/숨기기"));
  connect(btnToggleChannel, &QPushButton::clicked, this, [=]() {
    QList<int> sizes = mainSplitter->sizes();
    if (sizes[0] > 0) {
      sizes[1] += sizes[0];
      sizes[0] = 0;
      btnToggleChannel->setText(QString::fromUtf8("\xE2\x96\xB6"));
    } else {
      sizes[1] -= 220;
      sizes[0] = 220;
      btnToggleChannel->setText(QString::fromUtf8("\xE2\x97\x80"));
    }
    mainSplitter->setSizes(sizes);
  });

  QPushButton *btnToggleEvent =
      new QPushButton(QString::fromUtf8("\xE2\x96\xB6"), this);
  btnToggleEvent->setObjectName("btnToggleSidebar");
  btnToggleEvent->setFixedSize(24, 24);
  btnToggleEvent->setCursor(Qt::PointingHandCursor);
  btnToggleEvent->setToolTip(QString::fromUtf8("이벤트 로그 보이기/숨기기"));
  connect(btnToggleEvent, &QPushButton::clicked, this, [=]() {
    QList<int> sizes = mainSplitter->sizes();
    if (sizes[2] > 0) {
      sizes[1] += sizes[2];
      sizes[2] = 0;
      btnToggleEvent->setText(QString::fromUtf8("\xE2\x97\x80"));
    } else {
      sizes[1] -= 250;
      sizes[2] = 250;
      btnToggleEvent->setText(QString::fromUtf8("\xE2\x96\xB6"));
    }
    mainSplitter->setSizes(sizes);
  });

  toggleBarLayout->addWidget(btnToggleChannel);
  toggleBarLayout->addStretch();
  toggleBarLayout->addWidget(btnToggleEvent);
  centerLayout->addLayout(toggleBarLayout);

  QWidget *videoGridPanel = new QWidget(this);
  m_videoGridLayout = new QGridLayout(videoGridPanel);
  m_videoGridLayout->setContentsMargins(0, 0, 0, 0);
  m_videoGridLayout->setHorizontalSpacing(4);
  m_videoGridLayout->setVerticalSpacing(4);

  for (int i = 0; i < 4; ++i) {
    m_videoWidgets[i] = new VideoWidget(this);
    m_videoWidgets[i]->setVisible(false);
    m_videoWidgets[i]->setMinimumSize(320, 180);
    m_videoGridLayout->addWidget(m_videoWidgets[i], i / 2, i % 2);
  }
  m_videoGridLayout->setRowStretch(0, 1);
  m_videoGridLayout->setRowStretch(1, 1);
  m_videoGridLayout->setColumnStretch(0, 1);
  m_videoGridLayout->setColumnStretch(1, 1);

  centerLayout->addWidget(videoGridPanel, 1);

  // ── Right Panel: Event Log ──
  QFrame *eventPanel = new QFrame(this);
  eventPanel->setObjectName("eventPanel");
  eventPanel->setMinimumWidth(0); // 0으로 설정하여 완전 접기 허용
  eventPanel->setMaximumWidth(400);
  QVBoxLayout *eventPanelLayout = new QVBoxLayout(eventPanel);
  eventPanelLayout->setContentsMargins(12, 12, 12, 12);
  eventPanelLayout->setSpacing(8);

  QLabel *eventTitle = new QLabel(QString::fromUtf8("EVENT LOG"), this);
  eventTitle->setObjectName("panelTitle");
  eventPanelLayout->addWidget(eventTitle);

  m_eventListWidget = new QListWidget(this);
  m_eventListWidget->setAlternatingRowColors(false);
  m_eventListWidget->setWordWrap(true);
  m_eventListWidget->setSpacing(2);
  eventPanelLayout->addWidget(m_eventListWidget, 1);

  // 3-Panel 조합 (Splitter)
  mainSplitter->addWidget(channelScrollArea);
  mainSplitter->addWidget(centerPanel);
  mainSplitter->addWidget(eventPanel);
  mainSplitter->setCollapsible(0, true);  // 좌측 채널 패널 접기 가능
  mainSplitter->setCollapsible(1, false); // 중앙 패널은 접히지 않음
  mainSplitter->setCollapsible(2, true);  // 우측 이벤트 패널 접기 가능
  mainSplitter->setStretchFactor(
      1, 1); // 중앙 화면이 남는 공간을 모두 차지하도록 설정 (매우 중요)
  mainSplitter->setSizes({220, 850, 0});

  // 이제 Splitter 전체를 꽉 차게 cctvLayout에 바로 추가 (여백 없음)
  cctvLayout->addWidget(mainSplitter, 1);

  // ── Footer Bar ──
  QFrame *footerFrame = new QFrame(this);
  footerFrame->setObjectName("footerFrame");
  footerFrame->setFixedHeight(36);
  QHBoxLayout *footerLayout = new QHBoxLayout(footerFrame);
  footerLayout->setContentsMargins(16, 4, 16, 4);

  m_footerTimeLabel = new QLabel(this);
  m_footerTimeLabel->setObjectName("footerTime");
  m_footerTimeLabel->setText(
      QDateTime::currentDateTime().toString("yyyy/MM/dd  HH:mm:ss"));

  m_recordingDot = new QLabel(this);
  m_recordingDot->setObjectName("recordingDot");
  m_recordingDot->setFixedSize(10, 10);

  m_footerRecordingLabel = new QLabel(QString::fromUtf8("Recording"), this);
  m_footerRecordingLabel->setObjectName("recordingLabel");

  footerLayout->addWidget(m_footerTimeLabel);
  footerLayout->addStretch();
  footerLayout->addWidget(m_recordingDot);
  footerLayout->addSpacing(6);
  footerLayout->addWidget(m_footerRecordingLabel);
  cctvLayout->addWidget(footerFrame);

  // ======================
  // Tab 2: 텔레그램 테스트
  // ======================
  QWidget *telegramTab = new QWidget(this);
  QVBoxLayout *tgLayout = new QVBoxLayout(telegramTab);

  // 1. 상태 정보
  QGroupBox *statusGroup = new QGroupBox(QString::fromUtf8("상태 정보"), this);
  QFormLayout *statusForm = new QFormLayout();
  m_userCountLabel = new QLabel(QString::fromUtf8("- 명"), this);
  statusForm->addRow(QString::fromUtf8("등록된 사용자 수:"), m_userCountLabel);
  statusGroup->setLayout(statusForm);
  tgLayout->addWidget(statusGroup);

  // 2. 입차 알림 테스트
  QGroupBox *entryGroup =
      new QGroupBox(QString::fromUtf8("입차 알림 테스트"), this);
  QHBoxLayout *entryRow = new QHBoxLayout();
  m_entryPlateInput = new QLineEdit(this);
  m_entryPlateInput->setPlaceholderText(
      QString::fromUtf8("차량번호 입력 (예: 123가4567)"));
  m_btnSendEntry = new QPushButton(QString::fromUtf8("입차 알림 전송"), this);
  entryRow->addWidget(new QLabel(QString::fromUtf8("차량번호:"), this));
  entryRow->addWidget(m_entryPlateInput);
  entryRow->addWidget(m_btnSendEntry);
  entryGroup->setLayout(entryRow);
  tgLayout->addWidget(entryGroup);

  // 3. 출차 알림 테스트
  QGroupBox *exitGroup =
      new QGroupBox(QString::fromUtf8("출차 알림 테스트"), this);
  QHBoxLayout *exitRow = new QHBoxLayout();
  m_exitPlateInput = new QLineEdit(this);
  m_exitPlateInput->setPlaceholderText(
      QString::fromUtf8("차량번호 입력 (예: 123가4567)"));
  m_feeInput = new QSpinBox(this);
  m_feeInput->setRange(0, 999999);
  m_feeInput->setValue(5000);
  m_feeInput->setSuffix(QString::fromUtf8(" 원"));
  m_feeInput->setSingleStep(1000);

  m_btnSendExit = new QPushButton(QString::fromUtf8("출차 알림 전송"), this);

  exitRow->addWidget(new QLabel(QString::fromUtf8("차량번호:"), this));
  exitRow->addWidget(m_exitPlateInput);
  exitRow->addWidget(new QLabel(QString::fromUtf8("요금:"), this));
  exitRow->addWidget(m_feeInput);
  exitRow->addWidget(m_btnSendExit);
  exitGroup->setLayout(exitRow);
  tgLayout->addWidget(exitGroup);

  // 4. 등록된 사용자 목록
  QGroupBox *userListGroup =
      new QGroupBox(QString::fromUtf8("등록된 사용자 목록"), this);
  QVBoxLayout *userListLayout = new QVBoxLayout();
  m_userTable = new QTableWidget(this);
  m_userTable->setColumnCount(2);
  m_userTable->setHorizontalHeaderLabels(
      QStringList() << "Chat ID" << QString::fromUtf8("차량번호"));
  m_userTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  userListLayout->addWidget(m_userTable);
  userListGroup->setLayout(userListLayout);
  tgLayout->addWidget(userListGroup);

  tgLayout->addStretch();

  // Tab 3: 주차 DB 현황판
  // ======================
  QWidget *parkingDbTab = new QWidget(this);
  QVBoxLayout *dbLayout = new QVBoxLayout(parkingDbTab);

  QTabWidget *dbSubTabs = new QTabWidget(this);

  // --- Sub-Tab 1: 주차 이력 (Parking Logs) ---
  QWidget *logsTab = new QWidget(this);
  QVBoxLayout *logsLayout = new QVBoxLayout(logsTab);

  QHBoxLayout *logsToolBar = new QHBoxLayout();
  m_plateSearchInput = new QLineEdit(this);
  m_plateSearchInput->setPlaceholderText("번호판 검색...");
  m_btnSearchPlate = new QPushButton("검색", this);
  m_btnRefreshLogs = new QPushButton("새로고침", this);
  logsToolBar->addWidget(m_plateSearchInput);
  logsToolBar->addWidget(m_btnSearchPlate);
  logsToolBar->addWidget(m_btnRefreshLogs);
  logsToolBar->addStretch();

  m_parkingLogTable = new QTableWidget(this);
  m_parkingLogTable->setColumnCount(8);
  m_parkingLogTable->setHorizontalHeaderLabels(
      QStringList() << "ID" << "Object ID" << "번호판" << "구역명" << "입차시간"
                    << "출차시간" << "지불여부" << "총 금액");
  m_parkingLogTable->setColumnHidden(0, true);
  m_parkingLogTable->setColumnHidden(1, true);
  m_parkingLogTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);
  m_parkingLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_parkingLogTable->setSelectionBehavior(QAbstractItemView::SelectRows);

  logsLayout->addLayout(logsToolBar);
  logsLayout->addWidget(m_parkingLogTable);
  dbSubTabs->addTab(logsTab, "🚗 주차 이력");

  // --- Sub-Tab 2: 텔레그램 사용자 (Users) ---
  QWidget *usersTab = new QWidget(this);
  QVBoxLayout *usersLayout = new QVBoxLayout(usersTab);

  QHBoxLayout *usersToolBar = new QHBoxLayout();
  m_btnRefreshUsers = new QPushButton("새로고침", this);
  m_btnAddUser = new QPushButton("추가", this);
  m_btnEditUser = new QPushButton("수정", this);
  m_btnDeleteUser = new QPushButton("삭제", this);
  usersToolBar->addWidget(m_btnRefreshUsers);
  usersToolBar->addWidget(m_btnAddUser);
  usersToolBar->addWidget(m_btnEditUser);
  usersToolBar->addWidget(m_btnDeleteUser);
  usersToolBar->addStretch();

  m_userDbTable = new QTableWidget(this);
  m_userDbTable->setColumnCount(6);
  m_userDbTable->setHorizontalHeaderLabels(
      QStringList() << "Chat ID" << "번호판" << "이름" << "연락처" << "카드번호"
                    << "등록일");
  m_userDbTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_userDbTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_userDbTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_userDbTable->setSelectionMode(QAbstractItemView::SingleSelection);

  usersLayout->addLayout(usersToolBar);
  usersLayout->addWidget(m_userDbTable);
  dbSubTabs->addTab(usersTab, "👥 사용자");

  // --- Sub-Tab 3: 차량 정보 (Vehicles) ---
  QWidget *vhTab = new QWidget(this);
  QVBoxLayout *vhLayout = new QVBoxLayout(vhTab);
  vhLayout->setContentsMargins(0, 0, 0, 0);
  vhLayout->setSpacing(8);

  QLabel *reidSectionTitle =
      new QLabel(QString::fromUtf8("실시간 객체 정보 / 차량 정보 입력"), this);
  reidSectionTitle->setObjectName("panelTitle");
  vhLayout->addWidget(reidSectionTitle);

  m_reidTable = new QTableWidget(this);
  m_reidTable->setColumnCount(4);
  m_reidTable->setHorizontalHeaderLabels(QStringList()
                                         << QString::fromUtf8("채널")
                                         << "ReID"
                                         << "Obj ID"
                                         << QString::fromUtf8("번호판"));

  m_reidTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_reidTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_reidTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  vhLayout->addWidget(m_reidTable, 1);

  QGroupBox *forceGroup = new QGroupBox(
      QString::fromUtf8("선택 객체 정보 수정 (실험용 상세 제어)"), this);
  QVBoxLayout *forceLayout = new QVBoxLayout();

  QHBoxLayout *labelRow = new QHBoxLayout();
  labelRow->addWidget(new QLabel("Object ID", this), 1);
  labelRow->addWidget(new QLabel("Plate", this), 4);
  labelRow->addWidget(new QLabel("", this), 1);

  QHBoxLayout *inputRow = new QHBoxLayout();

  m_forceObjectIdInput = new QSpinBox(this);
  m_forceObjectIdInput->setRange(0, 2147483647);
  inputRow->addWidget(m_forceObjectIdInput, 1);

  m_forcePlateInput = new QLineEdit(this);
  m_forcePlateInput->setPlaceholderText("Plate");
  inputRow->addWidget(m_forcePlateInput, 4);

  m_btnForcePlate = new QPushButton(QString::fromUtf8("정보 업데이트"), this);
  inputRow->addWidget(m_btnForcePlate, 1);

  forceLayout->addLayout(labelRow);
  forceLayout->addLayout(inputRow);
  forceGroup->setLayout(forceLayout);
  vhLayout->addWidget(forceGroup);

  QGroupBox *settingsGroup =
      new QGroupBox(QString::fromUtf8("표시 및 보존 설정"), this);
  QHBoxLayout *settingsLayout = new QHBoxLayout();

  settingsLayout->addWidget(
      new QLabel(QString::fromUtf8("Stale Timeout (ms):"), this));
  m_staleTimeoutInput = new QSpinBox(this);
  m_staleTimeoutInput->setRange(0, 60000);
  m_staleTimeoutInput->setValue(1000);
  m_staleTimeoutInput->setSingleStep(500);
  settingsLayout->addWidget(m_staleTimeoutInput);

  settingsLayout->addSpacing(20);

  settingsLayout->addWidget(
      new QLabel(QString::fromUtf8("Prune Timeout (ms):"), this));
  m_pruneTimeoutInput = new QSpinBox(this);
  m_pruneTimeoutInput->setRange(0, 315360000);
  m_pruneTimeoutInput->setValue(5000);
  m_pruneTimeoutInput->setSingleStep(1000);
  settingsLayout->addWidget(m_pruneTimeoutInput);

  settingsLayout->addSpacing(20);
  m_chkShowStaleObjects =
      new QCheckBox(QString::fromUtf8("Stale 객체 표시"), this);
  m_chkShowStaleObjects->setChecked(true);
  settingsLayout->addWidget(m_chkShowStaleObjects);

  settingsLayout->addStretch();
  settingsGroup->setLayout(settingsLayout);
  vhLayout->addWidget(settingsGroup);
  dbSubTabs->addTab(vhTab, "🚘 차량 정보");

  // --- Sub-Tab 4: 주차구역 현황 (Zones) ---
  QWidget *zoneTab = new QWidget(this);
  QVBoxLayout *zoneLayout = new QVBoxLayout(zoneTab);

  QHBoxLayout *zoneToolBar = new QHBoxLayout();
  m_btnRefreshZone = new QPushButton("새로고침", this);
  zoneToolBar->addWidget(m_btnRefreshZone);
  zoneToolBar->addStretch();

  m_zoneTable = new QTableWidget(this);
  m_zoneTable->setColumnCount(4);
  m_zoneTable->setHorizontalHeaderLabels(QStringList()
                                         << "카메라" << "이름"
                                         << "점유" << "생성일");
  m_zoneTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_zoneTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_zoneTable->setSelectionBehavior(QAbstractItemView::SelectRows);

  zoneLayout->addLayout(zoneToolBar);
  zoneLayout->addWidget(m_zoneTable);
  dbSubTabs->addTab(zoneTab, "📍 주차구역 현황");

  dbLayout->addWidget(dbSubTabs);

  // ======================
  // Tab 5: 녹화 조회 (Recording Search)
  // ======================
  QWidget *recordTab = new QWidget(this);
  QVBoxLayout *recordLayout = new QVBoxLayout(recordTab);
  recordLayout->setSpacing(8);

  // ── Section A: 수동 제어 + 이벤트 구간 저장 (상단) ──────────
  QHBoxLayout *topControlArea = new QHBoxLayout();
  topControlArea->setSpacing(8);

  // [A-1] 수동 캡처/녹화 (가, 나 기능)
  QGroupBox *manualGroup =
      new QGroupBox(QString::fromUtf8("수동 캡처 / 녹화 제어"), this);
  QHBoxLayout *manualLayout = new QHBoxLayout(manualGroup);

  m_cmbManualCamera = new QComboBox(this);
  m_cmbManualCamera->addItem(QStringLiteral("Ch1"), 0);
  m_cmbManualCamera->addItem(QStringLiteral("Ch2"), 1);
  m_cmbManualCamera->addItem(QStringLiteral("Ch3"), 2);
  m_cmbManualCamera->addItem(QStringLiteral("Ch4"), 3);

  m_btnCaptureRecordTab =
      new QPushButton(QString::fromUtf8("📸 즉시 캡처"), this);
  m_btnRecordRecordTab =
      new QPushButton(QString::fromUtf8("⏺ 녹화 시작"), this);

  m_btnCaptureRecordTab->setMinimumHeight(32);
  m_btnRecordRecordTab->setMinimumHeight(32);
  m_cmbManualCamera->setMinimumHeight(32);
  m_btnRecordRecordTab->setCheckable(true);

  manualLayout->addWidget(m_cmbManualCamera);
  manualLayout->addWidget(m_btnCaptureRecordTab);
  manualLayout->addWidget(m_btnRecordRecordTab);
  manualLayout->addStretch();
  topControlArea->addWidget(manualGroup, 1);

  // [A-2] 이벤트 구간 녹화 테스트 (나 기능 - 전후 N초)
  QGroupBox *eventGroup =
      new QGroupBox(QString::fromUtf8("이벤트 구간 저장 테스트"), this);
  QHBoxLayout *eventLayout = new QHBoxLayout(eventGroup);

  // 이벤트 설명 (사용하지 않음, 컨트롤러 오류 방지를 위해 생성 후 숨김)
  m_recordEventTypeInput = new QLineEdit(hiddenContainer);

  eventLayout->addWidget(new QLabel(QString::fromUtf8("저장구간(초):"), this));
  m_recordIntervalSpin = new QSpinBox(this);
  m_recordIntervalSpin->setRange(2, 40);
  m_recordIntervalSpin->setValue(3);
  m_recordIntervalSpin->setMinimumHeight(32);
  eventLayout->addWidget(m_recordIntervalSpin);

  m_btnApplyEventSetting = new QPushButton(QString::fromUtf8("적용"), this);
  m_btnApplyEventSetting->setMinimumHeight(32);
  m_btnApplyEventSetting->setStyleSheet(
      "background: #4B5563; color: white; border-radius: 4px; font-weight: "
      "bold; padding: 0 12px;");
  eventLayout->addWidget(m_btnApplyEventSetting);

  eventLayout->addSpacing(8);

  m_btnTriggerEventRecord =
      new QPushButton(QString::fromUtf8("▶ 저장 실행"), this);
  m_btnTriggerEventRecord->setMinimumHeight(32);
  m_btnTriggerEventRecord->setStyleSheet(
      "background: #2563eb; color: white; border-radius: 4px; font-weight: "
      "bold;");

  eventLayout->addWidget(m_btnTriggerEventRecord);
  eventLayout->addStretch();
  topControlArea->addWidget(eventGroup, 1);

  // [A-3] 상시 녹화 제어 (다 기능 - 과거 영상 보기)
  QGroupBox *continuousGroup =
      new QGroupBox(QString::fromUtf8("상시 녹화 제어"), this);
  QHBoxLayout *continuousLayout = new QHBoxLayout(continuousGroup);

  continuousLayout->addWidget(
      new QLabel(QString::fromUtf8("녹화시간(분):"), this));
  m_spinRecordRetention = new QSpinBox(this);
  m_spinRecordRetention->setRange(1, 10080); // 최대 1주일(분 단위)
  m_spinRecordRetention->setValue(10);       // 기본 10분
  m_spinRecordRetention->setMinimumHeight(32);
  continuousLayout->addWidget(m_spinRecordRetention);

  m_btnApplyContinuousSetting =
      new QPushButton(QString::fromUtf8("적용"), this);
  m_btnApplyContinuousSetting->setMinimumHeight(32);
  m_btnApplyContinuousSetting->setStyleSheet(
      "background: #4B5563; color: white; border-radius: 4px; font-weight: "
      "bold; padding: 0 12px;");
  continuousLayout->addWidget(m_btnApplyContinuousSetting);

  m_lblContinuousStatus = new QLabel(QString::fromUtf8("녹화 중"), this);
  m_lblContinuousStatus->setStyleSheet(
      "color: #10b981; font-weight: bold; margin-left: 8px;");
  continuousLayout->addWidget(m_lblContinuousStatus);

  continuousLayout->addSpacing(16);

  m_btnViewContinuous = new QPushButton(QString::fromUtf8("▶ 상시영상"), this);
  m_btnViewContinuous->setMinimumHeight(32);
  m_btnViewContinuous->setStyleSheet(
      "background: #10b981; color: white; border-radius: 4px; font-weight: "
      "bold; padding: 0 16px;");
  continuousLayout->addWidget(m_btnViewContinuous);

  continuousLayout->addStretch();
  topControlArea->addWidget(continuousGroup, 1);

  recordLayout->addLayout(topControlArea);

  // ── Section B: 파일 목록 + 미리보기 (하단, 스플리터) ───────

  // 기록 목록 테이블 패널
  QWidget *listPanel = new QWidget(this);
  QVBoxLayout *listLayout = new QVBoxLayout(listPanel);
  listLayout->setSpacing(4);

  // 타이틀 + 관리 버튼 영역 (목록 위에만 배치)
  QHBoxLayout *titleRow = new QHBoxLayout();
  m_btnRefreshRecordLogs =
      new QPushButton(QString::fromUtf8("🔄 새로고침"), this);
  m_btnDeleteRecordLog =
      new QPushButton(QString::fromUtf8("🗑 선택 삭제"), this);

  QString topBtnStyle =
      "QPushButton { background: #334155; color: #CBD5E1; border: none; "
      "border-radius: 4px; padding: 4px 10px; font-size: 11px; }"
      "QPushButton:hover { background: #475569; color: white; }";
  m_btnRefreshRecordLogs->setStyleSheet(topBtnStyle);
  m_btnDeleteRecordLog->setStyleSheet(topBtnStyle);

  QLabel *tableTitle = new QLabel(QString::fromUtf8("저장된 미디어"), this);
  tableTitle->setStyleSheet("font-weight: bold; font-size: 13px;");

  titleRow->addWidget(tableTitle);
  titleRow->addSpacing(8);
  titleRow->addWidget(m_btnRefreshRecordLogs);
  titleRow->addSpacing(4);
  titleRow->addWidget(m_btnDeleteRecordLog);
  titleRow->addStretch();

  listLayout->addLayout(titleRow);
  listLayout->setSpacing(4);

  m_recordLogTable = new QTableWidget(this);
  m_recordLogTable->setColumnCount(3);
  m_recordLogTable->setHorizontalHeaderLabels(
      QStringList() << QString::fromUtf8("시간") << QString::fromUtf8("유형")
                    << QString::fromUtf8("설명"));

  m_recordLogTable->setColumnWidth(0, 135);
  m_recordLogTable->setColumnWidth(1, 70);
  m_recordLogTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::Stretch);
  m_recordLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_recordLogTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_recordLogTable->setAlternatingRowColors(true);

  listLayout->addWidget(m_recordLogTable, 1);

  m_recordPreviewPathLabel =
      new QLabel(QString::fromUtf8("선택된 파일: 없음"), this);
  m_recordPreviewPathLabel->setWordWrap(true);
  m_recordPreviewPathLabel->setStyleSheet(
      "font-size: 11px; padding: 4px; background: rgba(255,255,255,0.04); "
      "border-radius: 4px; color: #94A3B8;");

  listLayout->addWidget(m_recordPreviewPathLabel);

  // 미리보기 영역
  QWidget *previewPanel = new QWidget(this);
  QVBoxLayout *previewLayout = new QVBoxLayout(previewPanel);

  m_recordVideoWidget = new VideoWidget(this);
  m_recordVideoWidget->setMinimumWidth(320);
  m_recordVideoWidget->setMinimumHeight(200);
  previewLayout->addWidget(m_recordVideoWidget, 1);

  // ── 비디오 컨트롤 바 ──────────────────────────────
  QWidget *controlBarWidget = new QWidget(this);
  controlBarWidget->setStyleSheet(
      "QWidget { background: #1A2236; border-radius: 6px; padding: 2px; }");
  QVBoxLayout *controlBarLayout = new QVBoxLayout(controlBarWidget);
  controlBarLayout->setContentsMargins(6, 4, 6, 4);
  controlBarLayout->setSpacing(4);

  // 시크바
  m_videoSeekSlider = new QSlider(Qt::Horizontal, this);
  m_videoSeekSlider->setRange(0, 1000);
  m_videoSeekSlider->setValue(0);
  m_videoSeekSlider->setEnabled(false);
  m_videoSeekSlider->setStyleSheet(
      "QSlider::groove:horizontal { background: #334155; height: 6px; "
      "border-radius: 3px; }"
      "QSlider::handle:horizontal { background: #3B82F6; width: 14px; "
      "height: 14px; margin: -4px 0; border-radius: 7px; }"
      "QSlider::sub-page:horizontal { background: #3B82F6; "
      "border-radius: 3px; }");
  controlBarLayout->addWidget(m_videoSeekSlider);

  // 버튼 행
  QHBoxLayout *btnRow = new QHBoxLayout();
  btnRow->setSpacing(6);

  m_btnVideoPlay = new QPushButton(QString::fromUtf8("▶ 재생"), this);
  m_btnVideoPause = new QPushButton(QString::fromUtf8("⏸ 일시정지"), this);
  m_btnVideoStop = new QPushButton(QString::fromUtf8("⏹ 정지"), this);
  m_videoTimeLabel = new QLabel(QString::fromUtf8("00:00 / 00:00"), this);

  QString playerBtnStyle =
      "QPushButton { background: #334155; color: #CBD5E1; border: none; "
      "border-radius: 4px; padding: 5px 10px; font-size: 12px; min-width: "
      "70px; }"
      "QPushButton:hover { background: #3B82F6; color: white; }"
      "QPushButton:disabled { background: #1E293B; color: #475569; }";
  m_btnVideoPlay->setStyleSheet(playerBtnStyle);
  m_btnVideoPause->setStyleSheet(playerBtnStyle);
  m_btnVideoStop->setStyleSheet(playerBtnStyle);
  m_btnVideoPlay->setEnabled(false);
  m_btnVideoPause->setEnabled(false);
  m_btnVideoStop->setEnabled(false);
  m_videoTimeLabel->setStyleSheet(
      "color: #94A3B8; font-size: 12px; padding-left: 6px;");

  btnRow->addWidget(m_btnVideoPlay);
  btnRow->addWidget(m_btnVideoPause);
  btnRow->addWidget(m_btnVideoStop);
  btnRow->addStretch();
  btnRow->addWidget(m_videoTimeLabel);
  controlBarLayout->addLayout(btnRow);

  previewLayout->addWidget(controlBarWidget);

  QHBoxLayout *bottomLayout = new QHBoxLayout();
  bottomLayout->setContentsMargins(0, 0, 0, 0);
  bottomLayout->setSpacing(8);

  bottomLayout->addWidget(listPanel, 1);
  bottomLayout->addWidget(previewPanel, 2);

  recordLayout->addLayout(bottomLayout, 1);

  stackedWidget->addWidget(splashTab);
  stackedWidget->addWidget(cctvTab);
  stackedWidget->addWidget(telegramTab);
  stackedWidget->addWidget(parkingDbTab);
  stackedWidget->addWidget(recordTab);
  stackedWidget->setCurrentIndex(kSplashPageIndex);

  // 상위 레이아웃 구성
  layout->addWidget(stackedWidget, 1);

  // 로그 위젯은 컨트롤러에서 참조하므로 생성만 하고 숨김 처리
  m_chkShowPlateLogs = new QCheckBox(QString::fromUtf8("번호판 인식 로그 표시"),
                                     hiddenContainer);
  m_chkShowPlateLogs->setChecked(true);

  m_logView = new QTextEdit(hiddenContainer);
  m_logView->setReadOnly(true);

  showCctvSplash();
}

void MainWindow::showCctvSplash(const QString &message) {
  m_isCctvReady = false;
  if (m_splashMessageLabel && !message.isEmpty()) {
    m_splashMessageLabel->setText(message);
  }
  if (m_menuButton) {
    m_menuButton->setEnabled(false);
  }
  if (m_stackedWidget) {
    m_stackedWidget->setCurrentIndex(kSplashPageIndex);
  }
}

void MainWindow::showCctvPage() {
  m_isCctvReady = true;
  if (m_menuButton) {
    m_menuButton->setEnabled(true);
  }
  if (m_stackedWidget) {
    m_stackedWidget->setCurrentIndex(kCctvPageIndex);
  }
}

void MainWindow::openLogFilterSettings() {
  QDialog dlg(this);
  dlg.setWindowTitle(QString::fromUtf8("로그 필터 설정"));
  dlg.setFixedSize(300, 350);
  dlg.setStyleSheet(
      "QDialog { background: #1E293B; border: 1px solid #334155; }"
      "QLabel { color: #F1F5F9; font-size: 13px; }"
      "QCheckBox { color: #CBD5E1; font-size: 12px; padding: 4px 0; }"
      "QCheckBox::indicator { width: 16px; height: 16px; }"
      "QPushButton { background: #3B82F6; color: white; border: none; "
      "padding: 8px 16px; border-radius: 4px; font-size: 12px; }"
      "QPushButton:hover { background: #2563EB; }");

  QVBoxLayout *layout = new QVBoxLayout(&dlg);
  layout->setContentsMargins(20, 16, 20, 16);
  layout->setSpacing(6);

  QLabel *title =
      new QLabel(QString::fromUtf8("⚙️ 로그 출력 카테고리 설정"), &dlg);
  title->setStyleSheet("font-size: 15px; font-weight: bold; color: #F1F5F9;");
  layout->addWidget(title);
  layout->addSpacing(4);

  QLabel *desc =
      new QLabel(QString::fromUtf8("체크 해제하면 해당 카테고리의\n"
                                   "디버그 로그가 출력되지 않습니다."),
                 &dlg);
  desc->setStyleSheet("color: #94A3B8; font-size: 11px;");
  layout->addWidget(desc);
  layout->addSpacing(8);

  // 카테고리 표시 이름 (한글)
  QMap<QString, QString> displayNames;
  displayNames["OCR"] = QString::fromUtf8("OCR (번호판 인식)");
  displayNames["Video"] = QString::fromUtf8("Video (프레임 FPS)");
  displayNames["Camera"] = QString::fromUtf8("Camera (카메라 제어)");
  displayNames["Telegram"] = QString::fromUtf8("Telegram (텔레그램 봇)");
  displayNames["DB"] = QString::fromUtf8("DB (데이터베이스)");
  displayNames["ROI"] = QString::fromUtf8("ROI (구역 설정)");
  displayNames["OpenCV"] = QString::fromUtf8("OpenCV (내부 로그)");

  // 순서 고정
  QStringList order = {"OCR", "Video", "Camera", "Telegram",
                       "DB",  "ROI",   "OpenCV"};

  QMap<QString, QCheckBox *> checkboxes;
  LogFilterConfig &config = LogFilterConfig::instance();

  for (const QString &cat : order) {
    QCheckBox *chk = new QCheckBox(displayNames.value(cat, cat), &dlg);
    chk->setChecked(config.isEnabled(cat));
    layout->addWidget(chk);
    checkboxes[cat] = chk;
  }

  layout->addStretch();

  QPushButton *btnClose = new QPushButton(QString::fromUtf8("적용"), &dlg);
  layout->addWidget(btnClose);
  connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

  if (dlg.exec() == QDialog::Accepted) {
    for (auto it = checkboxes.begin(); it != checkboxes.end(); ++it) {
      config.setEnabled(it.key(), it.value()->isChecked());
    }
  }
}

#include "mainwindow.h"
#include "config/logfilterconfig.h"
#include "presentation/pages/cctvdashboardview.h"
#include "presentation/pages/cctvsplashpageview.h"
#include "presentation/pages/dbpageview.h"
#include "presentation/pages/recordpageview.h"
#include "presentation/pages/telegrampageview.h"
#include "presentation/controllers/mainwindowcontroller.h"
#include "presentation/shell/headerbarview.h"
#include "presentation/widgets/controllerdialog.h"
#include <QAction>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QToolButton>

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
#include <QLineEdit>
#include <QMap>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  // 프레임리스 윈도우 (타이틀바 제거)
  setWindowFlags(Qt::FramelessWindowHint);

  // UI 레이아웃 및 위젯 생성
  setupUi();

  // 종료 버튼이 존재하면 클릭 시 창 닫기
  if (m_headerView && m_headerView->uiRefs().btnExit) {
    connect(m_headerView->uiRefs().btnExit, &QPushButton::clicked, this,
            &MainWindow::close);
  }

  // 실시간 시계 타이머
  m_clockTimer = new QTimer(this);
  connect(m_clockTimer, &QTimer::timeout, this, [this]() {
    if (m_cctvView && m_cctvView->uiRefs().footerTimeLabel) {
      QDateTime now = QDateTime::currentDateTime();
      m_cctvView->uiRefs().footerTimeLabel->setText(
          now.toString("yyyy/MM/dd  HH:mm:ss"));
    }
  });
  m_clockTimer->start(1000);
  // 즉시 한 번 갱신
  if (m_cctvView && m_cctvView->uiRefs().footerTimeLabel) {
    m_cctvView->uiRefs().footerTimeLabel->setText(
        QDateTime::currentDateTime().toString("yyyy/MM/dd  HH:mm:ss"));
  }

  // 초기 창 크기 설정
  resize(1280, 720);
}

MainWindowUiRefs MainWindow::controllerUiRefs() const {
  MainWindowUiRefs uiRefs;
  static_cast<HeaderUiRefs &>(uiRefs) =
      m_headerView ? m_headerView->uiRefs() : HeaderUiRefs{};
  static_cast<SplashUiRefs &>(uiRefs) =
      m_splashView ? m_splashView->uiRefs() : SplashUiRefs{};
  static_cast<CctvUiRefs &>(uiRefs) =
      m_cctvView ? m_cctvView->uiRefs() : CctvUiRefs{};
  static_cast<TelegramUiRefs &>(uiRefs) =
      m_telegramView ? m_telegramView->uiRefs() : TelegramUiRefs{};
  static_cast<DbUiRefs &>(uiRefs) = m_dbView ? m_dbView->uiRefs() : DbUiRefs{};
  static_cast<RecordUiRefs &>(uiRefs) =
      m_recordView ? m_recordView->uiRefs() : RecordUiRefs{};
  uiRefs.stackedWidget = m_stackedWidget;
  return uiRefs;
}

void MainWindow::attachController(MainWindowController *controller) {
  m_controller = controller;
  if (!m_controller) {
    return;
  }
  if (!m_cctvView) {
    return;
  }
  if (m_controllerDialog) {
    m_controller->connectControllerDialog(m_controllerDialog);
  }

  auto updateFilter = [this]() {
    if (!m_controller)
      return;
    if (!m_cctvView)
      return;
    const CctvUiRefs &cctvUi = m_cctvView->uiRefs();
    QSet<QString> disabled;
    if (cctvUi.chkVehicle && !cctvUi.chkVehicle->isChecked()) {
      // 한화 카메라는 차종별로 타입을 보냄 (Vehical은 펌웨어 오타)
      disabled.insert("Vehicle");
      disabled.insert("Vehical");
      disabled.insert("Car");
      disabled.insert("Bus");
      disabled.insert("Truck");
      disabled.insert("Motorcycle");
      disabled.insert("Bicycle");
    }
    if (cctvUi.chkPlate && !cctvUi.chkPlate->isChecked())
      disabled.insert("LicensePlate");
    m_controller->updateObjectFilter(disabled);
  };
  const CctvUiRefs &cctvUi = m_cctvView->uiRefs();
  if (cctvUi.chkVehicle) {
    connect(cctvUi.chkVehicle, &QCheckBox::toggled, this, updateFilter);
  }
  if (cctvUi.chkPlate) {
    connect(cctvUi.chkPlate, &QCheckBox::toggled, this, updateFilter);
  }
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
  const HeaderUiRefs headerUi = m_headerView ? m_headerView->uiRefs()
                                             : HeaderUiRefs{};
  if (event->type() == QEvent::MouseButtonPress) {
    if (watched == headerUi.headerTitleLabel ||
        watched == headerUi.headerIconLabel ||
        (watched && watched->objectName() == "headerIcon")) {
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

  QVBoxLayout *layout = new QVBoxLayout(centralWidget);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  m_headerView = new HeaderBarView(this);
  const HeaderUiRefs &headerUi = m_headerView->uiRefs();

  layout->addWidget(m_headerView);

  QStackedWidget *stackedWidget = new QStackedWidget(this);
  m_stackedWidget = stackedWidget;

  if (headerUi.headerIconLabel) {
    headerUi.headerIconLabel->setCursor(Qt::PointingHandCursor);
    headerUi.headerIconLabel->installEventFilter(this);
  }
  if (headerUi.headerTitleLabel) {
    headerUi.headerTitleLabel->setCursor(Qt::PointingHandCursor);
    headerUi.headerTitleLabel->installEventFilter(this);
  }

  const struct {
    QString label;
    int tabIndex;
  } dbMenuActions[] = {
      {QString::fromUtf8("\xEC\xA3\xBC\xEC\xB0\xA8 \xEC\x9D\xB4\xEB\xA0\xA5"), 0},
      {QString::fromUtf8("\xEC\x82\xAC\xEC\x9A\xA9\xEC\x9E\x90"), 1},
      {QString::fromUtf8("\xEC\xB0\xA8\xEB\x9F\x89 \xEC\xA0\x95\xEB\xB3\xB4"), 2},
      {QString::fromUtf8(
           "\xEC\xA3\xBC\xEC\xB0\xA8\xEA\xB5\xAC\xEC\x97\xAD \xED\x98\x84\xED"
           "\x99\xA9"),
       3},
  };

  for (const auto &menuAction : dbMenuActions) {
    QAction *action = headerUi.navMenu->addAction(menuAction.label);
    connect(action, &QAction::triggered, this,
            [this, tabIndex = menuAction.tabIndex]() {
              navigateToDbSubTab(tabIndex);
            });
  }

  QAction *recordAction =
      headerUi.navMenu->addAction(QString::fromUtf8("REC"));
  connect(recordAction, &QAction::triggered, this, [this]() {
    navigateToPage(4);
  });

  QAction *ctrlAction = headerUi.navMenu->addAction(
      QString::fromUtf8("\xEC\xBB\xA8\xED\x8A\xB8\xEB\xA1\xA4\xEB\x9F\xAC"));
  connect(ctrlAction, &QAction::triggered, this, [this]() {
    if (!m_controllerDialog) {
      m_controllerDialog = new ControllerDialog(this);
      if (m_controller) {
        m_controller->connectControllerDialog(m_controllerDialog);
      }
    }
    m_controllerDialog->show();
    m_controllerDialog->raise();
    m_controllerDialog->activateWindow();
  });

  if (headerUi.settingsButton) {
    connect(headerUi.settingsButton, &QToolButton::clicked, this,
            &MainWindow::openLogFilterSettings);
  }

  if (headerUi.btnMinimize) {
    connect(headerUi.btnMinimize, &QPushButton::clicked, this,
            &MainWindow::showMinimized);
  }

  if (headerUi.btnMaxRestore) {
    connect(headerUi.btnMaxRestore, &QPushButton::clicked, this, [=]() {
      if (isMaximized()) {
        showNormal();
        headerUi.btnMaxRestore->setToolTip(
            QString::fromUtf8("\xEC\xB5\x9C\xEB\x8C\x80\xED\x99\x94"));
      } else {
        showMaximized();
        headerUi.btnMaxRestore->setToolTip(QString::fromUtf8(
            "\xEC\x9B\x90\xEB\x9E\x98 \xED\x81\xAC\xEA\xB8\xB0"));
      }
    });
  }

  m_splashView = new CctvSplashPageView(this);
  m_cctvView = new CctvDashboardView(this);
  const CctvUiRefs &cctvUi = m_cctvView->uiRefs();
  if (cctvUi.roiTargetCombo) {
    cctvUi.roiTargetCombo->installEventFilter(this);
  }
  if (cctvUi.roiNameEdit) {
    cctvUi.roiNameEdit->installEventFilter(this);
  }
  if (cctvUi.roiSelectorCombo) {
    cctvUi.roiSelectorCombo->installEventFilter(this);
  }

  m_telegramView = new TelegramPageView(this);
  m_dbView = new DbPageView(this);
  m_recordView = new RecordPageView(this);

  stackedWidget->addWidget(m_splashView);
  stackedWidget->addWidget(m_cctvView);
  stackedWidget->addWidget(m_telegramView);
  stackedWidget->addWidget(m_dbView);
  stackedWidget->addWidget(m_recordView);
  stackedWidget->setCurrentIndex(kSplashPageIndex);

  layout->addWidget(stackedWidget, 1);

  showCctvSplash();
}

void MainWindow::showCctvSplash(const QString &message) {
  m_isCctvReady = false;
  if (m_splashView && m_splashView->uiRefs().messageLabel &&
      !message.isEmpty()) {
    m_splashView->uiRefs().messageLabel->setText(message);
  }
  if (m_headerView && m_headerView->uiRefs().menuButton) {
    m_headerView->uiRefs().menuButton->setEnabled(false);
  }
  if (m_stackedWidget) {
    m_stackedWidget->setCurrentIndex(kSplashPageIndex);
  }
}

void MainWindow::showCctvPage() {
  m_isCctvReady = true;
  if (m_headerView && m_headerView->uiRefs().menuButton) {
    m_headerView->uiRefs().menuButton->setEnabled(true);
  }
  if (m_stackedWidget) {
    m_stackedWidget->setCurrentIndex(kCctvPageIndex);
  }
}

void MainWindow::navigateToPage(int stackedIndex) {
  if (m_stackedWidget && stackedIndex >= 0 &&
      stackedIndex < m_stackedWidget->count()) {
    m_stackedWidget->setCurrentIndex(stackedIndex);
  }
}

void MainWindow::navigateToDbSubTab(int tabIndex) {
  navigateToPage(kDbPageIndex);
  if (m_dbView && m_dbView->uiRefs().dbSubTabs && tabIndex >= 0 &&
      tabIndex < m_dbView->uiRefs().dbSubTabs->count()) {
    m_dbView->uiRefs().dbSubTabs->setCurrentIndex(tabIndex);
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
      new QLabel(QString::fromUtf8("로그 출력 카테고리 설정"), &dlg);
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

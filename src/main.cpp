#include "config/config.h"
#include "config/logfilterconfig.h"
#include "ui/windows/loginpage.h"
#include "ui/windows/mainwindow.h"
#include "ui/windows/mainwindowcontroller.h"
#include "video/videothread.h"
#include <opencv2/core/ocl.hpp>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFontDatabase>

// ── 카테고리 기반 로그 필터 핸들러 ──
static QtMessageHandler s_defaultHandler = nullptr;
static thread_local bool s_insideHandler = false;

static void filteredMessageHandler(QtMsgType type,
                                   const QMessageLogContext &ctx,
                                   const QString &msg) {
  if (s_insideHandler) {
    if (s_defaultHandler) {
      s_defaultHandler(type, ctx, msg);
    }
    return;
  }

  s_insideHandler = true;
  // 카테고리 감지 (싱글턴이 이미 초기화되어 있음을 가정)
  const QString cat = LogFilterConfig::instance().detectCategory(msg);
  s_insideHandler = false;

  if (!cat.isEmpty() && !LogFilterConfig::instance().isEnabled(cat)) {
    return; // 비활성화된 카테고리 → 출력 무시
  }

  // 기본 핸들러로 전달
  if (s_defaultHandler) {
    s_defaultHandler(type, ctx, msg);
  }
}

int main(int argc, char *argv[]) {
  // 싱글턴 강제 초기화 (메시지 핸들러 내 재귀 초기화 방지)
  LogFilterConfig::instance();

  // 메시지 핸들러 설치
  s_defaultHandler = qInstallMessageHandler(filteredMessageHandler);

  // 멀티스레드 MetaType 등록 (비디오 저장/이벤트용)
  qRegisterMetaType<QSharedPointer<cv::Mat>>("QSharedPointer<cv::Mat>");
  qRegisterMetaType<std::vector<QSharedPointer<cv::Mat>>>(
      "std::vector<QSharedPointer<cv::Mat>>");

  QApplication a(argc, argv);

  // === GPU(OpenCL) 가속 강제 활성화 ===
  // cv::UMat 연산(resize, cvtColor 등)이 Intel GPU에서 처리되도록 설정
  if (cv::ocl::haveOpenCL()) {
    cv::ocl::setUseOpenCL(true);
    cv::ocl::Device dev = cv::ocl::Device::getDefault();
    qDebug() << "[GPU] OpenCL enabled:"
             << QString::fromStdString(dev.name())
             << "| Vendor:" << QString::fromStdString(dev.vendorName())
             << "| Compute Units:" << dev.maxComputeUnits();
  } else {
    qWarning() << "[GPU] OpenCL NOT available. All image processing runs on CPU.";
  }

  // Load Hanwha fonts
  QDir fontDir(":/resources/fonts");
  for (const QString &fileName :
       fontDir.entryList(QStringList() << "*.ttf", QDir::Files)) {
    int id = QFontDatabase::addApplicationFont(":/resources/fonts/" + fileName);
    if (id == -1) {
      qWarning() << "Warning: Could not load application font:" << fileName;
    }
  }

  if (!Config::instance().load()) {
    qWarning() << "Warning: Could not load config file. Using default values.";
  }

  // Load dark theme stylesheet
  const QString qssPath = QDir(QCoreApplication::applicationDirPath())
                              .filePath("config/darktheme.qss");
  QFile qssFile(qssPath);
  if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
    a.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
    qssFile.close();
  } else {
    qWarning() << "Could not load stylesheet:" << qssPath;
  }

  MainWindow w;
  w.setWindowTitle(QStringLiteral("Veda Main"));

  LoginPage loginPage;
  bool isAuthenticated = false;

  MainWindowController *controller = nullptr;
  auto attachControllerIfNeeded = [&]() {
    if (controller) {
      return;
    }
    controller = new MainWindowController(w.controllerUiRefs(), &w);
    QObject::connect(controller, &MainWindowController::primaryVideoReady, &w,
                     &MainWindow::showCctvPage);
    w.attachController(controller);
  };

  QObject::connect(&loginPage, &LoginPage::loginSucceeded, &a, [&]() {
    isAuthenticated = true;
    attachControllerIfNeeded();
    w.showCctvSplash(QStringLiteral("CCTV 화면을 준비하고 있습니다..."));
    w.show();
    w.raise();
    w.activateWindow();
    if (controller) {
      controller->startInitialCctv();
    }
    // 2초 후 강제로 메인 화면(CCTV 페이지)으로 전환하는 타이머 추가
    QTimer::singleShot(2000, &w, [&]() { w.showCctvPage(); });
    loginPage.close();
  });

  QObject::connect(&loginPage, &LoginPage::loginClosed, &a, [&]() {
    if (!isAuthenticated) {
      a.quit();
    }
  });

  loginPage.show();
  return a.exec();
}

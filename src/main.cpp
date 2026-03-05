#include "config/config.h"
#include "ui/windows/loginpage.h"
#include "ui/windows/mainwindow.h"
#include "ui/windows/mainwindowcontroller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QDebug>

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);

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

#include "config/config.h"
#include "ui/windows/loginpage.h"
#include "ui/windows/mainwindow.h"
#include "ui/windows/mainwindowcontroller.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFontDatabase>

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
    w.attachController(controller);
  };

  // true면 앱 시작 시 로그인/메인 2개 창을 동시에 띄웁니다.
  constexpr bool kShowTwoWindowsOnStartup = true;
  if (kShowTwoWindowsOnStartup) {
    w.setEnabled(false);
    w.show();
  }

  QObject::connect(&loginPage, &LoginPage::loginSucceeded, &a, [&]() {
    isAuthenticated = true;
    attachControllerIfNeeded();
    w.setEnabled(true);
    w.show();
    w.raise();
    w.activateWindow();
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

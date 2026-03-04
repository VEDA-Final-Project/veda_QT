#include "config/config.h"
#include "ui/windows/mainwindow.h"
#include "ui/windows/mainwindowcontroller.h"


#include <QApplication>
#include <QDir>
#include <QFile>

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);

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
  MainWindowController *controller =
      new MainWindowController(w.controllerUiRefs(), &w);
  w.attachController(controller);
  w.show();
  return a.exec();
}

#include "mainwindow.h"
#include "config.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Load settings once at startup (falls back to defaults).
    
    if (!Config::instance().load()) {
        qWarning() << "Warning: Could not load config file. Using default values.";
    }

    MainWindow w;
    w.show();
    return a.exec();
}



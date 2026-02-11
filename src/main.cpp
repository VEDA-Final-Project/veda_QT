#include "core/config.h"
#include "ui/windows/mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    if (!Config::instance().load())
    {
        qWarning() << "Warning: Could not load config file. Using default values.";
    }

    MainWindow w;
    w.show();
    return a.exec();
}

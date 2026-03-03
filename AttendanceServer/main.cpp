#include "AttendanceServer.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    AttendanceServer window;
    window.show();
    return app.exec();
}

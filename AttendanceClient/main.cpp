#include "AttendanceClient.h"
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    // 实例化并显示登录窗口
    AttendanceClient loginWindow;
    loginWindow.show();

    return a.exec();
}
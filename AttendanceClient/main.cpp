#include "AttendanceClient.h"
#include <QtWidgets/QApplication>
#include <QNetworkProxyFactory>

int main(int argc, char* argv[])
{
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    QApplication a(argc, argv);
    // 显示登录窗口
    AttendanceClient loginWindow;
    loginWindow.show();
    return a.exec();
}
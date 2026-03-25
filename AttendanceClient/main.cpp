#include "AttendanceClient.h"
#include <QtWidgets/QApplication>
#include <QNetworkProxyFactory>

int main(int argc, char* argv[])
{
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    QApplication a(argc, argv);
    // 实例化并显示登录窗口
    AttendanceClient loginWindow;
    loginWindow.show();
    return a.exec();
}
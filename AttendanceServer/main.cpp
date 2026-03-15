#include "AttendanceServer.h"
#include "ServerLogin.h"  // 引入登录界面的头文件
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // 实例化主窗口和登录窗口
    AttendanceServer mainWindow;
    ServerLogin loginWindow;
    // 核心跳转逻辑：捕捉登录界面的“成功”信号
    QObject::connect(&loginWindow, &ServerLogin::loginSuccessful, [&]() {
        mainWindow.show();    // 显示主窗口
        loginWindow.close();  // 关闭并销毁登录窗口
        });
    // 程序启动时，只显示登录窗口
    loginWindow.show();

    return app.exec();
}
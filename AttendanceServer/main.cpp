#include "AttendanceServer.h"
#include "ServerLogin.h"
#include "ConfigEncryptor.h"       // 问题四：加密配置文件读写器
#include <QtWidgets/QApplication>
#include <QSslSocket>
#include <QFile>
#include <QJsonObject>
#include <QDebug>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QString configPath = QCoreApplication::applicationDirPath() + "/server_config.enc";
    if (!QFile::exists(configPath)) {
        qDebug() << "[Main] 未检测到加密配置文件，首次运行自动生成...";

        QJsonObject config;
        config["db_host"] = "127.0.0.1";   // ← 你的 MySQL 主机地址
        config["db_port"] = "3305";         // ← 你的 MySQL 端口号
        config["db_user"] = "faceserver";         // ← 你的数据库用户名
        config["db_pwd"] = "qwer1234";         // ← 你的数据库密码
        if (ConfigEncryptor::saveEncryptedConfig(configPath, config)) {
            qDebug() << "[Main] server_config.enc 已自动生成:" << configPath;
        }
        else {
            qWarning() << "[Main] 加密配置文件生成失败！请检查环境变量 ATTENDANCE_CONFIG_KEY 是否已设置。";
        }
    }
    else {
        qDebug() << "[Main] 加密配置文件已存在，跳过生成。";
    }
    // 实例化主窗口和登录窗口
    AttendanceServer mainWindow;
    ServerLogin loginWindow;
    // 核心跳转逻辑：捕捉登录界面的"成功"信号
    QObject::connect(&loginWindow, &ServerLogin::loginSuccessful, [&]() {
        mainWindow.show();    // 显示主窗口
        loginWindow.close();  // 关闭并销毁登录窗口
        });
    // 程序启动时，只显示登录窗口
    loginWindow.show();
    return app.exec();
}
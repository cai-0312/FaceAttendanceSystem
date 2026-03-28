#include "ConfigEncryptor.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QDebug>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QJsonObject config;
    config["db_host"] = "127.0.0.1";       // 你的 MySQL 地址
    config["db_port"] = "3305";             // 你的 MySQL 端口
    config["db_user"] = "root";             // 数据库用户名
    config["db_pwd"] = "root";             // 数据库密码

    QString path = QCoreApplication::applicationDirPath() + "/server_config.enc";

    if (ConfigEncryptor::saveEncryptedConfig(path, config)) {
        qDebug() << "配置文件生成成功:" << path;
    }
    else {
        qDebug() << "生成失败！请检查环境变量 ATTENDANCE_CONFIG_KEY 是否已设置";
    }
    return 0;
}
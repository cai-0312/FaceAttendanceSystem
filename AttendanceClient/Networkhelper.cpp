#include "NetworkHelper.h"
#include <QTcpSocket>
#include <QThread>
#include <QDebug>
#include <QtConcurrent>

QString  NetworkHelper::s_serverIp = QStringLiteral("127.0.0.1");
quint16  NetworkHelper::s_serverPort = 9999;

void NetworkHelper::setServer(const QString& ip, quint16 port)
{
    s_serverIp = ip;
    s_serverPort = port;
    qDebug() << "[NetworkHelper] 服务器地址已配置为:" << ip << ":" << port;
}

QString NetworkHelper::serverIp() { return s_serverIp; }
quint16 NetworkHelper::serverPort() { return s_serverPort; }

QJsonObject NetworkHelper::request(const QJsonObject& jsonRequest, int connectTimeout, int readTimeout)
{
    QTcpSocket socket;
    socket.connectToHost(s_serverIp, s_serverPort);
    QJsonObject responseJson;

    QString reqType = jsonRequest["type"].toString();

    if (!socket.waitForConnected(connectTimeout)) {
        qDebug() << "[NetworkHelper] 连接失败!";
        return responseJson;
    }

    QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
    socket.write(block);
    socket.waitForBytesWritten(2000);

    QByteArray responseData;
    // 🚀 核心改进：拆除 100 次的 retryCount 陷阱，只要没有超时断开，就安心等完整换行符
    while (socket.waitForReadyRead(readTimeout)) {
        responseData += socket.readAll();

        // 只要数据包包含了服务端发来的 \n 结束符，就代表接收完成
        if (responseData.contains("\n")) {
            QJsonDocument checkDoc = QJsonDocument::fromJson(responseData.trimmed());
            if (!checkDoc.isNull() && checkDoc.isObject()) {
                responseJson = checkDoc.object();
                break;
            }
        }
    }

    if (responseJson.isEmpty()) {
        qDebug() << "[NetworkHelper] 解析失败或截断！type=" << reqType << "收到字节=" << responseData.size();
    }

    socket.disconnectFromHost();
    return responseJson;
}

void NetworkHelper::sendAsync(const QJsonObject& jsonRequest)
{
    QString ip = s_serverIp;
    quint16 port = s_serverPort;
    QString reqType = jsonRequest["type"].toString();

    QtConcurrent::run([jsonRequest, ip, port, reqType]() {
        QTcpSocket socket;
        socket.connectToHost(ip, port);
        if (socket.waitForConnected(1500)) {
            QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
            socket.write(block);
            socket.waitForBytesWritten(1000);
            socket.disconnectFromHost();
        }
        });
}

void NetworkHelper::sendReliable(const QJsonObject& jsonRequest)
{
    QTcpSocket socket;
    socket.connectToHost(s_serverIp, s_serverPort);
    if (socket.waitForConnected(1500)) {
        QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
        socket.write(block);
        while (socket.bytesToWrite() > 0) {
            socket.waitForBytesWritten(100);
        }
        socket.flush();
        QThread::msleep(300);
        socket.disconnectFromHost();
    }
}
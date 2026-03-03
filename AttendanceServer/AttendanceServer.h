#pragma once
#include <QWidget>
#include <QSqlQueryModel>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QDateTime>

// ★ 补全所有缺失的 Qt 控件头文件，消除 incomplete type 报错
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QTextBrowser>

// ★ 注意：这里必须和你的 ui 文件名对应。如果你 ui 叫 AttendanceServer.ui，这里就是 ui_AttendanceServer.h
#include "ui_AttendanceServer.h"

// 定义一个结构体，用来保存连上来的客户端信息
struct ClientInfo {
    QString name;
    QString dept;
    QString ip;
    QString loginTime;
};

class AttendanceServer : public QWidget {
    Q_OBJECT

public:
    explicit AttendanceServer(QWidget* parent = nullptr);
    ~AttendanceServer();

private slots:
    // 数据中心按钮
    void on_btn_RefreshData_clicked();
    void on_btn_ExportGlobal_clicked();

    // 网络控制台按钮
    void on_btn_StartServer_clicked();
    void on_btn_StopServer_clicked();

    // TCP 底层事件槽函数
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    void initDatabase();
    void loadGlobalRecords();
    void logMessage(const QString& msg); // 专属的彩色日志打印工具
    void updateOnlineUsersTable();       // 刷新左侧在线人员表格

    Ui::AttendanceServerClass* ui;
    QSqlQueryModel* m_globalRecordModel;

    // TCP 核心组件
    QTcpServer* m_tcpServer;

    // 路由表：Socket指针 -> 客户端详细信息
    QMap<QTcpSocket*, ClientInfo> m_clients;
    // 快捷路由表：人名 -> Socket指针 (用于极速转发聊天消息)
    QMap<QString, QTcpSocket*> m_nameToSocket;
};
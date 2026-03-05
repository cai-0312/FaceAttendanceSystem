#pragma once
#include <QWidget>
#include <QSqlQueryModel>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QDateTime>

#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QTextBrowser>

#include "ui_AttendanceServer.h"

// 客户端信息结构体：用于维护当前在线用户的身份信息与连接属性
struct ClientInfo {
    QString name;       // 员工姓名
    QString dept;       // 所属部门
    QString ip;         // 客户端IP地址
    QString loginTime;  // 建立连接的时间
};

class AttendanceServer : public QWidget {
    Q_OBJECT

public:
    explicit AttendanceServer(QWidget* parent = nullptr);
    ~AttendanceServer();

private slots:
    // 数据中心：刷新全局考勤记录列表
    void on_btn_RefreshData_clicked();
    // 数据中心：导出全体员工的考勤报表
    void on_btn_ExportGlobal_clicked();

    // 网络控制：启动TCP监听服务
    void on_btn_StartServer_clicked();
    // 网络控制：停止并断开所有当前连接
    void on_btn_StopServer_clicked();

    // TCP底层事件：处理新的客户端连接请求
    void onNewConnection();
    // TCP底层事件：接收并解析客户端发送的JSON协议报文
    void onReadyRead();
    // TCP底层事件：处理客户端主动或意外断开连接的情况
    void onClientDisconnected();

private:
    // 初始化服务器本地数据库连接
    void initDatabase();
    // 加载并显示全局考勤统计数据模型
    void loadGlobalRecords();
    // 日志系统：向服务器控制台输出彩色格式化运行日志
    void logMessage(const QString& msg);
    // UI更新：实时刷新左侧在线人员列表视图
    void updateOnlineUsersTable();

    // 界面类指针
    Ui::AttendanceServerClass* ui;
    // 全局数据查询模型
    QSqlQueryModel* m_globalRecordModel;

    // TCP服务端核心监听对象
    QTcpServer* m_tcpServer;

    // 映射表：套接字指针到客户端详细信息的关联（用于管理连接）
    QMap<QTcpSocket*, ClientInfo> m_clients;
    // 映射表：用户名到套接字指针的关联（用于消息定向转发）
    QMap<QString, QTcpSocket*> m_nameToSocket;
};
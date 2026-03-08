#ifndef ATTENDANCESERVER_H
#define ATTENDANCESERVER_H

#include <QWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QSqlQueryModel>
#include <QSqlTableModel>
#include "ui_AttendanceServer.h"

struct ClientInfo {
    QString name;
    QString dept;
    QString jobTitle;
    QString ip;
    QString loginTime;
};

class AttendanceServer : public QWidget
{
    Q_OBJECT

public:
    AttendanceServer(QWidget* parent = nullptr);
    ~AttendanceServer();

private slots:
    void on_btn_StartServer_clicked();
    void on_btn_StopServer_clicked();
    void on_btn_RefreshData_clicked();
    void on_btn_ExportGlobal_clicked();
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    Ui::AttendanceServerClass* ui;
    QTcpServer* m_tcpServer;
    QMap<QTcpSocket*, ClientInfo> m_clients;
    QMap<QString, QTcpSocket*> m_nameToSocket;

    QSqlQueryModel* m_globalRecordModel;
    QSqlTableModel* m_permModel; // 🚀 新增：RBAC 权限管理模型

    void initDatabase();
    void updateOnlineUsersTable();
    void loadGlobalRecords();

public slots:
    // 允许跨线程调用的日志打印函数
    void logMessage(const QString& msg);
};

#endif // ATTENDANCESERVER_H
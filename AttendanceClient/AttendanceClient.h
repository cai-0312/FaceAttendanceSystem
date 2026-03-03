#pragma once

#include <QtWidgets/QWidget>
#include "ui_AttendanceClient.h"
class MainWidget;

namespace Ui { class AttendanceClientClass; }
class AttendanceClient : public QWidget
{
    Q_OBJECT

public:
    AttendanceClient(QWidget* parent = nullptr);
    ~AttendanceClient();

    
private slots:
    void on_btn_Login_clicked();             // 登录界面的“登录”按钮
    void on_btn_GoRegister_clicked();        // 登录界面的“去注册”按钮（翻页）
    void on_btn_ConfirmRegister_clicked();   // 注册界面的“确认注册”按钮（写数据库）
    void on_btn_BackLogin_clicked();         // 注册界面的“返回登录”按钮（翻页）
    

private:
    Ui::AttendanceClientClass *ui;
    MainWidget* mainWindow = nullptr;
};
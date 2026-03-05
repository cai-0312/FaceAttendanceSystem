#pragma once

#include <QtWidgets/QWidget>
#include <QMouseEvent>
#include <QPoint>
#include <QAction> 
#include "ui_AttendanceClient.h"

class MainWidget;
namespace Ui { class AttendanceClientClass; }

class AttendanceClient : public QWidget
{
    Q_OBJECT

public:
    AttendanceClient(QWidget* parent = nullptr);
    ~AttendanceClient();

protected:
    // 鼠标按下事件：记录坐标以实现无边框窗口拖动
    void mousePressEvent(QMouseEvent* event) override;
    // 鼠标移动事件：计算偏移并实时移动窗口
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    // 登录按钮点击槽函数：验证用户信息并进入主界面
    void on_btn_Login_clicked();
    // 跳转注册页面槽函数
    void on_btn_GoRegister_clicked();
    // 确认注册按钮槽函数：将新用户信息写入数据库
    void on_btn_ConfirmRegister_clicked();
    // 返回登录页面槽函数
    void on_btn_BackLogin_clicked();

    // 窗口关闭按钮槽函数
    void on_btn_Close_clicked();
    // 窗口最小化按钮槽函数
    void on_btn_Min_clicked();

    // 密码可见性切换槽函数：控制密码框明文与密文状态的转换
    void togglePasswordVisibility();

private:
    // 指向由UI设计器自动生成的界面类指针
    Ui::AttendanceClientClass* ui;
    // 主功能窗口指针：登录成功后跳转的目标
    MainWidget* mainWindow = nullptr;

    // 记录鼠标拖拽窗口时的初始位置
    QPoint m_dragPos;

    // 密码框右侧图标动作指针（小眼睛图标）
    QAction* m_pwdAction;
    // 标志位：记录当前密码是否为可见状态
    bool m_isPwdVisible;
};
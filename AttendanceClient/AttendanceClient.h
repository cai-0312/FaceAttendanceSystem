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
    explicit AttendanceClient(QWidget* parent = nullptr); // 初始化登录界面
    ~AttendanceClient(); // 释放登录界面资源
    void showLoginReady(); // 显示登录界面
protected:
    void mousePressEvent(QMouseEvent* event) override; // 记录鼠标按下位置
    void mouseMoveEvent(QMouseEvent* event) override; // 处理窗口拖动
private slots:
    void on_btn_Login_clicked(); // 处理登录按钮点击
    void on_btn_GoRegister_clicked(); // 打开注册页面
    void on_btn_ConfirmRegister_clicked(); // 提交注册信息
    void on_btn_BackLogin_clicked(); // 返回登录页面
    void on_btn_Close_clicked(); // 关闭应用窗口
    void on_btn_Min_clicked(); // 最小化窗口
    void togglePasswordVisibility(); // 切换密码显示状态
private:
    Ui::AttendanceClientClass* ui; // 界面对象指针
    MainWidget* mainWindow = nullptr; // 主界面指针
    QPoint m_dragPos; // 拖动偏移量
    QAction* m_pwdAction; // 密码显示切换动作
    bool m_isPwdVisible; // 密码是否可见
};
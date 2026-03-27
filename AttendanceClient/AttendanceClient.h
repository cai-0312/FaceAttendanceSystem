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
    // 初始化考勤客户端登录界面
    explicit AttendanceClient(QWidget* parent = nullptr);
    // 释放UI指针资源
    ~AttendanceClient();
    void showLoginReady();                                         // 退出登录后重新显示登录界面
protected:
    void mousePressEvent(QMouseEvent* event) override;             // 记录坐标以实现无边框窗口拖动
    void mouseMoveEvent(QMouseEvent* event) override;              // 计算偏移并实时移动窗口
private slots:
    void on_btn_Login_clicked();                                   // 验证用户信息并进入主界面
    void on_btn_GoRegister_clicked();                              // 跳转至注册页面
    void on_btn_ConfirmRegister_clicked();                         // 将新用户信息写入数据库并处理权限分配
    void on_btn_BackLogin_clicked();                               // 返回至登录页面
    void on_btn_Close_clicked();                                   // 窗口关闭按钮点击事件
    void on_btn_Min_clicked();                                     // 窗口最小化按钮点击事件
    void togglePasswordVisibility();                               // 控制密码框明文与密文状态的转换
private:
    Ui::AttendanceClientClass* ui;                                 // 指向由UI设计器自动生成的界面类
    MainWidget* mainWindow = nullptr;                              // 主功能窗口，登录成功后跳转的目标
    QPoint m_dragPos;                                              // 记录鼠标拖拽窗口时的初始位置
    QAction* m_pwdAction;                                          // 密码框右侧图标动作（小眼睛图标）
    bool m_isPwdVisible;                                           // 记录当前密码是否为可见状态
};
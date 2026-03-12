#pragma once

#include <QWidget>
#include <QPoint>
#include <QMouseEvent>
// 1. 提前声明 Ui 命名空间中的 UI 类（避免直接在头文件中 include ui_xxx.h 造成重定义）
namespace Ui {
    class ServerLoginClass;
}

class ServerLogin : public QWidget
{
    Q_OBJECT

public:
    explicit ServerLogin(QWidget* parent = nullptr);
    ~ServerLogin();

signals:
    // 声明一个登录成功信号
    void loginSuccessful();
protected:
    // 拦截鼠标按下和移动事件
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void on_btn_ServerLogin_clicked();

private:
    // 2. 注意这里必须带有星号 *，声明为指针
    Ui::ServerLoginClass* ui;
    QPoint m_dragPosition; // 记录鼠标拖动时的相对坐标


};

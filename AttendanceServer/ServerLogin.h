#ifndef SERVERLOGIN_H
#define SERVERLOGIN_H
#include <QWidget>
#include <QPoint>
#include <QMouseEvent>
namespace Ui { class ServerLoginClass; }                                    
class ServerLogin : public QWidget
{
    Q_OBJECT
public:
    explicit ServerLogin(QWidget* parent = nullptr);                        // 初始化服务器登录界面容器并配置底层窗口属性
    ~ServerLogin();                                                         // 释放内存资源
signals:
    void loginSuccessful();                                                   // 当数据库鉴权通过时触发，通知主控循环拉起核心业务窗口
protected:
    void mousePressEvent(QMouseEvent* event) override;                       // 重写鼠标按下事件，用于记录无边框窗口拖拽的初始相对坐标偏移量
    void mouseMoveEvent(QMouseEvent* event) override;                        // 重写鼠标移动事件，结合初始偏移量动态计算并更新主窗口的屏幕绝对位置
private slots:
    void on_btn_ServerLogin_clicked();                                       // 登录按钮点击事件，执行数据库连接与系统管理员身份的合法性校验
private:
    Ui::ServerLoginClass* ui;                                                // 由UI设计器自动生成的服务端登录界面控件句柄
    QPoint m_dragPosition;                                                   // 记录鼠标拖动无边框窗口时的相对坐标信息
};
#endif // SERVERLOGIN_H
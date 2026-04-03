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
    explicit ServerLogin(QWidget* parent = nullptr);                        // 初始化登录窗口并配置窗口属性
    ~ServerLogin();                                                         // 析构并释放 UI 资源
signals:
    void loginSuccessful();                                                  // 登录成功信号，通知主控启动主界面
protected:
    void mousePressEvent(QMouseEvent* event) override;                       // 记录鼠标按下时的窗口内偏移以支持拖拽
    void mouseMoveEvent(QMouseEvent* event) override;                        // 根据鼠标移动更新窗口位置以实现拖拽
private slots:
    void on_btn_ServerLogin_clicked();                                       // 处理登录按钮点击并验证管理员身份
private:
    Ui::ServerLoginClass* ui;                                                // 指向由 UI 生成的界面句柄
    QPoint m_dragPosition;                                                   // 存储窗口拖拽时的鼠标偏移
};
#endif // SERVERLOGIN_H
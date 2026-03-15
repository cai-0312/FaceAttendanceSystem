#ifndef NOTICEPOPUP_H
#define NOTICEPOPUP_H
#include <QWidget>
#include <QPropertyAnimation>
#include <QTimer>
class NoticePopup : public QWidget
{
    Q_OBJECT
public:
    explicit NoticePopup(const QString& title, const QString& content, QWidget* parent = nullptr);  // 构造函数：初始化通知气泡弹窗，设置无边框与置顶属性
    void showAnimation();                           // 动画展示：计算主屏幕右下角坐标并执行向上滑出的缓动动画
private:
    QPropertyAnimation* m_animation;                // 动画对象：控制弹窗从屏幕外滑动至可视区域的位置属性动画
    QTimer* m_timer;                                // 定时器对象：控制弹窗在指定驻留时间后自动关闭并销毁
};
#endif // NOTICEPOPUP_H
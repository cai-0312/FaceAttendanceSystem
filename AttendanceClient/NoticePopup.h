#ifndef NOTICEPOPUP_H
#define NOTICEPOPUP_H
#include <QWidget>
#include <QPropertyAnimation>
#include <QTimer>
class NoticePopup : public QWidget
{
    Q_OBJECT
public:
    // 初始化通知弹窗
    explicit NoticePopup(const QString& title, const QString& content, QWidget* parent = nullptr); 
    void showAnimation(); // 显示并播放弹出动画
private:
    QPropertyAnimation* m_animation; // 弹出动画对象
    QTimer* m_timer; // 自动关闭定时器
};
#endif
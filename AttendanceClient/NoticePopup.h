#pragma once
#include <QWidget>
#include <QPropertyAnimation>
#include <QTimer>

class NoticePopup : public QWidget
{
    Q_OBJECT
public:
    explicit NoticePopup(const QString& title, const QString& content, QWidget* parent = nullptr);
    // 触发弹出动画
    void showAnimation();

private:
    QPropertyAnimation* m_animation;
    QTimer* m_timer;
};
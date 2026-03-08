#include "NoticePopup.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>

NoticePopup::NoticePopup(const QString& title, const QString& content, QWidget* parent)
    : QWidget(parent)
{
    // 设置无边框、工具窗口（不在任务栏显示）、置顶、且不抢占当前用户的输入焦点
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose); // 关闭时自动释放内存
    setAttribute(Qt::WA_ShowWithoutActivating);

    setFixedSize(320, 150);

    // 背景卡片设计
    QFrame* bgFrame = new QFrame(this);
    bgFrame->setStyleSheet("QFrame { background-color: #FFFFFF; border-radius: 8px; border: 1px solid #E4E7ED; }");
    bgFrame->setGeometry(10, 10, 300, 130);

    // 阴影特效
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 40));
    shadow->setBlurRadius(15);
    bgFrame->setGraphicsEffect(shadow);

    QVBoxLayout* layout = new QVBoxLayout(bgFrame);
    layout->setContentsMargins(15, 15, 15, 15);

    // 标题
    QLabel* titleLabel = new QLabel("📢 " + title, bgFrame);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #F56C6C; border: none;");

    // 正文
    QLabel* contentLabel = new QLabel(content, bgFrame);
    contentLabel->setStyleSheet("font-size: 13px; color: #606266; border: none;");
    contentLabel->setWordWrap(true);
    contentLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    layout->addWidget(titleLabel);
    layout->addWidget(contentLabel);
    layout->addStretch();

    m_animation = new QPropertyAnimation(this, "pos");
    m_timer = new QTimer(this);

    // 8秒后自动关闭
    connect(m_timer, &QTimer::timeout, this, [=]() { this->close(); });
}

void NoticePopup::showAnimation() {
    // 获取当前主屏幕的工作区域（除去任务栏）
    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();

    // 计算右下角位置
    int endX = screenRect.width() - this->width() - 10;
    int endY = screenRect.height() - this->height() - 10;
    int startY = screenRect.height(); // 初始位置在屏幕可视区域外

    this->move(endX, startY);
    this->show();

    // 执行滑动动画
    m_animation->setDuration(600);
    m_animation->setStartValue(QPoint(endX, startY));
    m_animation->setEndValue(QPoint(endX, endY));
    m_animation->setEasingCurve(QEasingCurve::OutBack);
    m_animation->start();

    m_timer->start(8000);
}
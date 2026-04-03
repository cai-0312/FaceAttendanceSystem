#include "NoticePopup.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
// 构造函数，初始化通知弹窗和界面布局
NoticePopup::NoticePopup(const QString& title, const QString& content, QWidget* parent)
    : QWidget(parent)
{
    // 设置窗口属性
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(320, 150);
    // 创建背景卡片
    QFrame* bgFrame = new QFrame(this);
    bgFrame->setStyleSheet("QFrame { background-color: #FFFFFF; border-radius: 8px; border: 1px solid #E4E7ED; }");
    bgFrame->setGeometry(10, 10, 300, 130);
    // 添加阴影效果
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 40));
    shadow->setBlurRadius(15);
    bgFrame->setGraphicsEffect(shadow);
    // 组装内容布局
    QVBoxLayout* layout = new QVBoxLayout(bgFrame);
    layout->setContentsMargins(15, 15, 15, 15);
    // 标题文本
    QLabel* titleLabel = new QLabel(title, bgFrame);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #F56C6C; border: none;");
    // 正文文本
    QLabel* contentLabel = new QLabel(content, bgFrame);
    contentLabel->setStyleSheet("font-size: 13px; color: #606266; border: none;");
    contentLabel->setWordWrap(true);
    contentLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(titleLabel);
    layout->addWidget(contentLabel);
    layout->addStretch();
    // 初始化动画和定时器
    m_animation = new QPropertyAnimation(this, "pos");
    m_timer = new QTimer(this);
    // 超时后自动关闭
    connect(m_timer, &QTimer::timeout, this, [=]() { this->close(); });
}
// 显示弹窗并执行入场动画
void NoticePopup::showAnimation() {
    // 获取屏幕可用区域
    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    // 计算起始位置和目标位置
    int endX = screenRect.width() - this->width() - 10;
    int endY = screenRect.height() - this->height() - 10;
    int startY = screenRect.height();
    this->move(endX, startY);
    this->show();
    // 执行滑动动画
    m_animation->setDuration(600);
    m_animation->setStartValue(QPoint(endX, startY));
    m_animation->setEndValue(QPoint(endX, endY));
    m_animation->setEasingCurve(QEasingCurve::OutBack);
    m_animation->start();
    // 启动自动关闭计时器
    m_timer->start(8000);
}
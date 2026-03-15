#include "NoticePopup.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
// 构造函数：初始化系统通知气泡弹窗，配置底层窗口属性及UI布局结构
NoticePopup::NoticePopup(const QString& title, const QString& content, QWidget* parent)
    : QWidget(parent)
{
    // 窗口属性配置：无边框、工具窗口（不在任务栏显示）、系统级置顶、且不抢占当前用户的键盘输入焦点
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(320, 150);
    // 背景卡片设计：构建圆角白色卡片作为通知内容的挂载基底
    QFrame* bgFrame = new QFrame(this);
    bgFrame->setStyleSheet("QFrame { background-color: #FFFFFF; border-radius: 8px; border: 1px solid #E4E7ED; }");
    bgFrame->setGeometry(10, 10, 300, 130);
    // 阴影特效配置：为通知卡片添加边缘模糊阴影，增强视觉层级感
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 40));
    shadow->setBlurRadius(15);
    bgFrame->setGraphicsEffect(shadow);
    QVBoxLayout* layout = new QVBoxLayout(bgFrame);
    layout->setContentsMargins(15, 15, 15, 15);
    // 标题组件：展示系统通知的摘要信息
    QLabel* titleLabel = new QLabel("📢 " + title, bgFrame);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #F56C6C; border: none;");
    // 正文组件：展示系统通知的详细内容，并支持自动换行
    QLabel* contentLabel = new QLabel(content, bgFrame);
    contentLabel->setStyleSheet("font-size: 13px; color: #606266; border: none;");
    contentLabel->setWordWrap(true);
    contentLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(titleLabel);
    layout->addWidget(contentLabel);
    layout->addStretch();
    m_animation = new QPropertyAnimation(this, "pos");
    m_timer = new QTimer(this);
    // 定时销毁机制：绑定超时信号，在展示 8 秒后自动关闭弹窗并触发内存回收
    connect(m_timer, &QTimer::timeout, this, [=]() { this->close(); });
}
// 动画展示控制：计算屏幕边界坐标并执行平滑的滑动入场动画
void NoticePopup::showAnimation() {
    // 获取当前主屏幕的有效工作区域边界（自动排除操作系统的任务栏占用区域）
    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    // 坐标计算：设定目标展示位置为屏幕右下角，初始起始位置在屏幕可视区域的正下方外侧
    int endX = screenRect.width() - this->width() - 10;
    int endY = screenRect.height() - this->height() - 10;
    int startY = screenRect.height();
    this->move(endX, startY);
    this->show();
    // 缓动动画执行：设置持续时间为 600 毫秒的非线性回弹滑动效果
    m_animation->setDuration(600);
    m_animation->setStartValue(QPoint(endX, startY));
    m_animation->setEndValue(QPoint(endX, endY));
    m_animation->setEasingCurve(QEasingCurve::OutBack);
    m_animation->start();
    // 启动驻留倒计时定时器
    m_timer->start(8000);
}
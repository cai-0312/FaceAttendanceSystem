#include "HomeModule.h"
#include "NetworkHelper.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QListWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QChartView>
#include <QPieSeries>
#include <QBarSeries>
#include <QBarSet>
#include <QLineSeries>
#include <QBarCategoryAxis>
#include <QValueAxis>
// 构造函数：初始化仪表盘主界面容器，并挂载至父级布局中
HomeModule::HomeModule(QVBoxLayout* mainLayout, QString role, QString loginName, QObject* parent)
    : QObject(parent), m_mainLayout(mainLayout), m_role(role), m_loginName(loginName)
{
    m_dashboardWidget = new QWidget();
    m_dashboardLayout = new QVBoxLayout(m_dashboardWidget);
    m_dashboardLayout->setContentsMargins(0, 10, 0, 0);
    m_dashboardLayout->setSpacing(15);

    m_mainLayout->addWidget(m_dashboardWidget);
}
HomeModule::~HomeModule() {}
// 递归清理指定的布局容器，确保大屏刷新时动态生成的旧控件被正确释放
void HomeModule::clearLayout(QLayout* layout) {
    if (!layout) return;
    QLayoutItem* child;
    while ((child = layout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        else if (child->layout()) {
            clearLayout(child->layout());
        }
        delete child;
    }
}
// 刷新大屏数据：向服务器拉取最新的聚合数据，并按模块依次渲染
void HomeModule::refreshDashboard() {
    clearLayout(m_dashboardLayout);

    QJsonObject req;
    req["type"] = "query_home_dashboard";
    req["role"] = m_role;
    req["name"] = m_loginName;
    // 发起网络数据异步请求
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        // 渲染顶部核心数据概览卡片
        renderTopCards(m_dashboardLayout, res["top_cards"].toObject());
        // 渲染中间数据可视化图表区域
        renderMiddleCharts(m_dashboardLayout, res);
        // 渲染底部实时打卡动态与公告待办面板
        renderBottomFeed(m_dashboardLayout, res);
    }
    else {
        // 若服务器未就绪，渲染空数据面板防止出现白屏
        renderTopCards(m_dashboardLayout, QJsonObject());
    }
}
// 渲染顶部核心数据速览卡片区域
void HomeModule::renderTopCards(QVBoxLayout* parentLayout, const QJsonObject& data) {
    QHBoxLayout* cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(15);
    int totalExpected = data["total_expected"].toInt();
    int actualPunched = data["actual_punched"].toInt();
    int abnormalCount = data["abnormal_count"].toInt();
    QString rateStr = totalExpected > 0 ? QString::number((actualPunched * 100) / totalExpected) + "%" : "0%";
    // 渲染基础考勤数据卡片
    cardsLayout->addWidget(createDataCard("今日出勤率", rateStr, "较昨日持平", "#00B42A"));
    cardsLayout->addWidget(createDataCard("实到 / 应到人数", QString("%1 / %2").arg(actualPunched).arg(totalExpected), "人脸核验打卡", "#3370FF"));
    cardsLayout->addWidget(createDataCard("今日异常人数", QString::number(abnormalCount) + " 人", "迟到/早退/旷工", "#F53F3F"));
    // 根据角色权限渲染对应的待办或个人异常卡片
    if (m_role.contains("管理员") || m_role == "经理") {
        int pendingLeaves = data["pending_leaves"].toInt();
        int pendingAppeals = data["pending_appeals"].toInt();
        cardsLayout->addWidget(createDataCard("我的审批待办", QString::number(pendingLeaves + pendingAppeals) + " 项",
            QString("请假:%1 | 申诉:%2").arg(pendingLeaves).arg(pendingAppeals), "#FF7D00"));
    }
    else {
        int myAbnormal = data["my_abnormal"].toInt();
        cardsLayout->addWidget(createDataCard("本月我的异常", QString::number(myAbnormal) + " 次", "申诉处理", "#722ED1"));
    }
    parentLayout->addLayout(cardsLayout, 1);
}
// 通过代码动态构建单张数据展示卡片
QFrame* HomeModule::createDataCard(const QString& title, const QString& value, const QString& subText, const QString& colorHex) {
    QFrame* card = new QFrame();
    card->setStyleSheet("QFrame { background-color: white; border-radius: 8px; border: 1px solid #E5E6EB; }");
    QVBoxLayout* lay = new QVBoxLayout(card);
    QLabel* lblTitle = new QLabel(title);
    lblTitle->setStyleSheet("color: #86909C; font-size: 14px; font-weight: bold; border: none;");
    QLabel* lblValue = new QLabel(value);
    lblValue->setStyleSheet(QString("color: %1; font-size: 28px; font-weight: 900; border: none;").arg(colorHex));
    QLabel* lblSub = new QLabel(subText);
    lblSub->setStyleSheet("color: #C9CDD4; font-size: 12px; border: none;");
    lay->addWidget(lblTitle);
    lay->addStretch();
    lay->addWidget(lblValue);
    lay->addWidget(lblSub);
    return card;
}
// 渲染中间数据可视化图表区域
void HomeModule::renderMiddleCharts(QVBoxLayout* parentLayout, const QJsonObject& res) {
    QHBoxLayout* chartsLayout = new QHBoxLayout();
    chartsLayout->setSpacing(15);
    chartsLayout->addWidget(createPieChart(res["pie_chart"].toArray()), 1);
    chartsLayout->addWidget(createBarChart(res["bar_chart"].toArray()), 1);
    chartsLayout->addWidget(createLineChart(res["line_chart"].toArray()), 1);
    parentLayout->addLayout(chartsLayout, 3);
}
// 动态生成今日出勤状态的分布饼状图
QChartView* HomeModule::createPieChart(const QJsonArray& data) {
    QChart* chart = new QChart();
    QPieSeries* series = new QPieSeries();
    for (int i = 0; i < data.size(); ++i) {
        QJsonObject o = data[i].toObject();
        series->append(o["status"].toString() + " (" + QString::number(o["count"].toInt()) + ")", o["count"].toInt());
    }
    if (series->count() == 0) series->append("暂无数据", 1);
    for (auto slice : series->slices()) {
        slice->setLabelVisible(true);
        if (slice->label().contains("正常")) slice->setBrush(QColor("#00B42A"));
        else if (slice->label().contains("迟到") || slice->label().contains("早退")) slice->setBrush(QColor("#F53F3F"));
        else slice->setBrush(QColor("#FF7D00"));
    }
    chart->addSeries(series);
    chart->setTitle("今日实时出勤占比");
    chart->legend()->hide();
    chart->setMargins(QMargins(0, 0, 0, 0));
    QChartView* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setStyleSheet("background-color: white; border-radius: 8px; border: 1px solid #E5E6EB;");
    return view;
}
// 动态生成各部门异常人次排名的柱状图
QChartView* HomeModule::createBarChart(const QJsonArray& data) {
    QChart* chart = new QChart();
    QBarSeries* series = new QBarSeries();
    QBarSet* setAbnormal = new QBarSet("异常人次");
    setAbnormal->setColor(QColor("#3370FF"));
    QStringList categories;
    for (int i = 0; i < data.size(); ++i) {
        QJsonObject o = data[i].toObject();
        categories << o["dept"].toString();
        *setAbnormal << o["count"].toInt();
    }
    if (categories.isEmpty()) { categories << "无"; *setAbnormal << 0; }
    series->append(setAbnormal);
    chart->addSeries(series);
    chart->setTitle("近30天各部门异常排名");
    QBarCategoryAxis* axisX = new QBarCategoryAxis(); axisX->append(categories);
    chart->addAxis(axisX, Qt::AlignBottom); series->attachAxis(axisX);
    QValueAxis* axisY = new QValueAxis(); axisY->setLabelFormat("%d");
    chart->addAxis(axisY, Qt::AlignLeft); series->attachAxis(axisY);
    chart->setMargins(QMargins(0, 0, 0, 0));
    QChartView* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setStyleSheet("background-color: white; border-radius: 8px; border: 1px solid #E5E6EB;");
    return view;
}
// 动态生成近期每日出勤热度趋势的折线图
QChartView* HomeModule::createLineChart(const QJsonArray& data) {
    QChart* chart = new QChart();
    QLineSeries* series = new QLineSeries();
    QPen pen(QColor("#F53F3F")); pen.setWidth(3); series->setPen(pen);
    QStringList days; int index = 0;
    for (int i = 0; i < data.size(); ++i) {
        QJsonObject o = data[i].toObject();
        days << o["date"].toString();
        series->append(index++, o["count"].toInt());
    }
    if (days.isEmpty()) { days << "-"; series->append(0, 0); }
    chart->addSeries(series);
    chart->setTitle("近7日出勤热度趋势");
    QBarCategoryAxis* axisX = new QBarCategoryAxis(); axisX->append(days);
    chart->addAxis(axisX, Qt::AlignBottom); series->attachAxis(axisX);
    QValueAxis* axisY = new QValueAxis(); axisY->setLabelFormat("%d");
    chart->addAxis(axisY, Qt::AlignLeft); series->attachAxis(axisY);
    chart->setMargins(QMargins(0, 0, 0, 0));
    QChartView* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setStyleSheet("background-color: white; border-radius: 8px; border: 1px solid #E5E6EB;");
    return view;
}
// 渲染底部实时动态与系统公告区域
void HomeModule::renderBottomFeed(QVBoxLayout* parentLayout, const QJsonObject& res) {
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(15);
    // 构建左侧实时打卡流水线看板
    QFrame* feedFrame = new QFrame();
    feedFrame->setStyleSheet("background-color: white; border-radius: 8px; border: 1px solid #E5E6EB;");
    QVBoxLayout* feedLay = new QVBoxLayout(feedFrame);
    QLabel* feedTitle = new QLabel("实时打卡动态");
    feedTitle->setStyleSheet("font-weight: bold; font-size: 14px; border: none;");
    QListWidget* feedList = new QListWidget();
    feedList->setStyleSheet("border: none; background: transparent; font-size: 13px;");
    QJsonArray feedArr = res["feed_list"].toArray();
    for (int i = 0; i < feedArr.size(); ++i) {
        QJsonObject o = feedArr[i].toObject();
        QString st = o["status"].toString();
        // 界面字符串保留表情以增强用户视觉反馈
        QString icon = st.contains("正常") ? "🟢" : (st.contains("请假") ? "🟡" : "🔴");
        feedList->addItem(QString("[%1] %2 %3 %4").arg(o["time"].toString(), o["name"].toString(), icon, st));
    }
    if (feedList->count() == 0) feedList->addItem("今日暂无打卡动态...");
    feedLay->addWidget(feedTitle);
    feedLay->addWidget(feedList);
    bottomLayout->addWidget(feedFrame, 2);
    // 构建右侧系统公告与业务操作快捷入口
    QFrame* rightFrame = new QFrame();
    rightFrame->setStyleSheet("background-color: white; border-radius: 8px; border: 1px solid #E5E6EB;");
    QVBoxLayout* rightLay = new QVBoxLayout(rightFrame);
    QLabel* noticeTitle = new QLabel("系统公告看板");
    noticeTitle->setStyleSheet("font-weight: bold; font-size: 14px; border: none;");
    QListWidget* noticeList = new QListWidget();
    noticeList->setStyleSheet("border: none; background: transparent; font-size: 13px; color: #4E5969;");
    QJsonArray noticeArr = res["notice_list"].toArray();
    for (int i = 0; i < noticeArr.size(); ++i) {
        QJsonObject o = noticeArr[i].toObject();
        noticeList->addItem(QString("📢 [%1] %2").arg(o["date"].toString(), o["content"].toString()));
    }
    if (noticeList->count() == 0) noticeList->addItem("暂无最新公告...");
    rightLay->addWidget(noticeTitle);
    rightLay->addWidget(noticeList);
    QHBoxLayout* btnLay = new QHBoxLayout();
    if (!m_role.contains("管理员") && m_role != "经理") {
        // 普通员工视角：展示快捷请假与申诉按钮
        QPushButton* btnLeave = new QPushButton("快捷请假");
        QPushButton* btnAppeal = new QPushButton("异常申诉");
        btnLeave->setStyleSheet("background-color: #3370FF; color: white; border-radius: 4px; padding: 8px; font-weight: bold;");
        btnAppeal->setStyleSheet("background-color: #FF7D00; color: white; border-radius: 4px; padding: 8px; font-weight: bold;");
        btnLeave->setCursor(Qt::PointingHandCursor);
        btnAppeal->setCursor(Qt::PointingHandCursor);
        connect(btnLeave, &QPushButton::clicked, this, &HomeModule::requestQuickLeave);
        connect(btnAppeal, &QPushButton::clicked, this, &HomeModule::requestQuickAppeal);
        btnLay->addWidget(btnLeave);
        btnLay->addWidget(btnAppeal);
    }
    else {
        // 管理员及经理视角：展示流程审批按钮
        QPushButton* btnApproveLeave = new QPushButton("审批请假单");
        QPushButton* btnApproveAppeal = new QPushButton("审批异常申诉");
        btnApproveLeave->setStyleSheet("background-color: #00B42A; color: white; border-radius: 4px; padding: 8px; font-weight: bold;");
        btnApproveAppeal->setStyleSheet("background-color: #F53F3F; color: white; border-radius: 4px; padding: 8px; font-weight: bold;");
        btnApproveLeave->setCursor(Qt::PointingHandCursor);
        btnApproveAppeal->setCursor(Qt::PointingHandCursor);
        connect(btnApproveLeave, &QPushButton::clicked, this, &HomeModule::requestApproveLeave);
        connect(btnApproveAppeal, &QPushButton::clicked, this, &HomeModule::requestApproveAppeal);
        btnLay->addWidget(btnApproveLeave);
        btnLay->addWidget(btnApproveAppeal);
    }
    rightLay->addLayout(btnLay);
    bottomLayout->addWidget(rightFrame, 1);
    parentLayout->addLayout(bottomLayout, 2);
}
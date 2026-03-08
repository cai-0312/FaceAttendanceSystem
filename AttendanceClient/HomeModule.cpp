#include "HomeModule.h"
#include <QDebug>
#include <QDateTime>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// 🚀 核心通讯组件：绝不绕过服务器，首页只拿 JSON 不碰数据库
static QJsonObject requestDataFromServer(const QJsonObject& jsonRequest) {
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", 9999);
    QJsonObject responseJson;
    if (socket.waitForConnected(2000)) {
        QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
        socket.write(block);
        socket.waitForBytesWritten(1000);
        // 首页数据量可能稍大，拉长等待时间并确保读取完整
        if (socket.waitForReadyRead(5000)) {
            QByteArray responseData;
            while (socket.waitForReadyRead(50) || socket.bytesAvailable() > 0) {
                responseData += socket.readAll();
                if (responseData.endsWith("\n")) break;
            }
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (!doc.isNull() && doc.isObject()) responseJson = doc.object();
        }
        socket.disconnectFromHost();
    }
    return responseJson;
}

HomeModule::HomeModule(QVBoxLayout* mainLayout, QString role, QString loginName, QObject* parent)
    : QObject(parent), m_mainLayout(mainLayout), m_role(role), m_loginName(loginName)
{
    m_dashboardWidget = new QWidget();
    m_dashboardLayout = new QVBoxLayout(m_dashboardWidget);
    m_dashboardLayout->setContentsMargins(0, 10, 0, 0);
    m_dashboardLayout->setSpacing(15);

    m_mainLayout->addWidget(m_dashboardWidget);
    // 建表操作已移交服务端 initDatabase()
}

HomeModule::~HomeModule() {}

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

void HomeModule::refreshDashboard() {
    clearLayout(m_dashboardLayout);

    // 🚀 向服务器发起大屏数据总请求
    QJsonObject req;
    req["type"] = "query_home_dashboard";
    req["role"] = m_role;
    req["name"] = m_loginName;
    QJsonObject res = requestDataFromServer(req);

    if (res["status"].toString() == "success") {
        // 1. 渲染顶部四个核心数据卡片
        renderTopCards(m_dashboardLayout, res["top_cards"].toObject());
        // 2. 渲染中间三个可视化图表
        renderMiddleCharts(m_dashboardLayout, res);
        // 3. 渲染底部实时动态与公告待办
        renderBottomFeed(m_dashboardLayout, res);
    }
    else {
        // 若服务器未就绪，渲染空数据面板防止白屏
        renderTopCards(m_dashboardLayout, QJsonObject());
    }
}

// ==========================================
// 🎨 1. 顶部核心数据速览卡片
// ==========================================
void HomeModule::renderTopCards(QVBoxLayout* parentLayout, const QJsonObject& data) {
    QHBoxLayout* cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(15);

    int totalExpected = data["total_expected"].toInt();
    int actualPunched = data["actual_punched"].toInt();
    int abnormalCount = data["abnormal_count"].toInt();
    QString rateStr = totalExpected > 0 ? QString::number((actualPunched * 100) / totalExpected) + "%" : "0%";

    // 卡片1：出勤率
    cardsLayout->addWidget(createDataCard("今日出勤率", rateStr, "较昨日持平", "#00B42A"));
    // 卡片2：实到/应到
    cardsLayout->addWidget(createDataCard("实到 / 应到人数", QString("%1 / %2").arg(actualPunched).arg(totalExpected), "人脸核验打卡", "#3370FF"));
    // 卡片3：异常统计
    cardsLayout->addWidget(createDataCard("今日异常人数", QString::number(abnormalCount) + " 人", "迟到/早退/旷工", "#F53F3F"));

    // 卡片4：千人千面 (待办/个人状态)
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

// ==========================================
// 📊 2. 中间三大核心图表
// ==========================================
void HomeModule::renderMiddleCharts(QVBoxLayout* parentLayout, const QJsonObject& res) {
    QHBoxLayout* chartsLayout = new QHBoxLayout();
    chartsLayout->setSpacing(15);

    chartsLayout->addWidget(createPieChart(res["pie_chart"].toArray()), 1);
    chartsLayout->addWidget(createBarChart(res["bar_chart"].toArray()), 1);
    chartsLayout->addWidget(createLineChart(res["line_chart"].toArray()), 1);

    parentLayout->addLayout(chartsLayout, 3);
}

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

// ==========================================
// 🔔 3. 底部动态与操作区
// ==========================================
void HomeModule::renderBottomFeed(QVBoxLayout* parentLayout, const QJsonObject& res) {
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(15);

    // 左侧：实时打卡流水线
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
        QString icon = st.contains("正常") ? "🟢" : (st.contains("请假") ? "🟡" : "🔴");
        feedList->addItem(QString("[%1] %2 %3 %4").arg(o["time"].toString(), o["name"].toString(), icon, st));
    }
    if (feedList->count() == 0) feedList->addItem("今日暂无打卡动态...");

    feedLay->addWidget(feedTitle);
    feedLay->addWidget(feedList);
    bottomLayout->addWidget(feedFrame, 2);

    // 右侧：系统公告与快捷入口
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
        // 【普通员工视角】
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
        // 🚀 【管理员/超级管理员视角】
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
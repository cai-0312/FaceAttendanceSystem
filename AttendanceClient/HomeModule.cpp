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
#include <QComboBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QToolTip>
#include <QCursor>
#include <QChartView>
#include <QPieSeries>
#include <QBarSeries>
#include <QAbstractBarSeries>
#include <QBarSet>
#include <QLineSeries>
#include <QBarCategoryAxis>
#include <QValueAxis>
// 构造函数，初始化布局、角色、用户名、部门、职务等信息，创建仪表盘容器
HomeModule::HomeModule(QVBoxLayout* mainLayout, QString role, QString loginName,
    QString department, QString jobTitle, QObject* parent)
    : QObject(parent), m_mainLayout(mainLayout), m_role(role), m_loginName(loginName),
    m_department(department), m_jobTitle(jobTitle), m_timeRange("本自然月"), m_filterDept("全部")
{
    m_dashboardWidget = new QWidget();
    m_dashboardLayout = new QVBoxLayout(m_dashboardWidget);
    m_dashboardLayout->setContentsMargins(0, 10, 0, 0);
    m_dashboardLayout->setSpacing(15);
    m_mainLayout->addWidget(m_dashboardWidget);
}
HomeModule::~HomeModule() {}
// 递归清空布局中的所有子控件和子布局
void HomeModule::clearLayout(QLayout* layout) {
    if (!layout) return;
    QLayoutItem* child;
    while ((child = layout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        else if (child->layout()) clearLayout(child->layout());
        delete child;
    }
}
// 刷新仪表盘，清空旧内容后重新请求服务端数据并渲染各区域
void HomeModule::refreshDashboard() {
    clearLayout(m_dashboardLayout);
    // 首次刷新时向服务端查询当前用户的部门和职务
    if (!m_deptFetched) {
        QJsonObject uReq;
        uReq["type"] = "query_user_dept";
        uReq["name"] = m_loginName;
        QJsonObject uRes = NetworkHelper::request(uReq);
        QString fetchedDept = uRes["department"].toString();
        QString fetchedJob = uRes["job_title"].toString();
        if (!fetchedDept.isEmpty()) m_department = fetchedDept;
        if (!fetchedJob.isEmpty()) m_jobTitle = fetchedJob;
        m_deptFetched = true;
        qDebug() << "[HomeModule] Fetched dept=" << m_department << "jobTitle=" << m_jobTitle;
    }
    renderToolBar(m_dashboardLayout);
    // 构造请求参数，包含角色、部门、时间维度、筛选部门等
    QJsonObject req;
    req["type"] = "query_home_dashboard";
    req["role"] = m_role;
    req["name"] = m_loginName;
    req["department"] = m_department;
    req["job_title"] = m_jobTitle;
    req["time_range"] = m_timeRange;
    req["filter_dept"] = m_filterDept;
    // 请求成功则渲染卡片、图表、底部动态，否则显示空卡片
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        renderTopCards(m_dashboardLayout, res["top_cards"].toObject());
        renderMiddleCharts(m_dashboardLayout, res);
        renderBottomFeed(m_dashboardLayout, res);
    }
    else {
        renderTopCards(m_dashboardLayout, QJsonObject());
    }
}
// 渲染顶部工具栏，包含时间周期选择、部门筛选和刷新按钮
void HomeModule::renderToolBar(QVBoxLayout* parentLayout) {
    QHBoxLayout* toolLay = new QHBoxLayout();
    toolLay->setSpacing(10);
    QString iconBase = "../../AttendanceClient/icon_library/";
    QLabel* timeIcon = new QLabel();
    timeIcon->setPixmap(QIcon(iconBase + "Home/icon_calendar.svg").pixmap(18, 18));
    timeIcon->setStyleSheet("border:none;");
    QLabel* timeLabel = new QLabel("统计周期:");
    timeLabel->setStyleSheet("font-size:13px; font-weight:bold; color:#4E5969; border:none;");
    toolLay->addWidget(timeIcon);
    toolLay->addWidget(timeLabel);
    // 时间周期下拉框，切换后自动刷新仪表盘
    m_timeCombo = new QComboBox();
    m_timeCombo->addItems({ "本周", "本自然月", "近7天", "近30天" });
    m_timeCombo->setCurrentText(m_timeRange);
    m_timeCombo->setStyleSheet("QComboBox { border:1px solid #DCDFE6; border-radius:4px; padding:4px 10px; min-width:100px; }");
    m_timeCombo->setStyleSheet(
        "QComboBox { border:1px solid #DCDFE6; border-radius:4px; padding:4px 10px; "
        "min-width:140px; background:white; color:#303133; font-size:13px; }"
        "QComboBox:hover { border-color:#409EFF; }"
        "QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:25px; border-left:none; }"
        "QComboBox::down-arrow { image:none; border-left:5px solid transparent; border-right:5px solid transparent; border-top:5px solid #909399; }"
    );
    connect(m_timeCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        m_timeRange = text;
        refreshDashboard();
        });
    toolLay->addWidget(m_timeCombo);
    // 部门筛选下拉框，仅人力资源部经理可见
    bool isHRManager = (m_department == "人力资源部" && m_jobTitle == "部门经理");
    qDebug() << "[HomeModule] dept=" << m_department << "jobTitle=" << m_jobTitle << "isHRManager=" << isHRManager;
    if (isHRManager) {
        QLabel* deptIcon = new QLabel();
        deptIcon->setPixmap(QIcon(iconBase + "Home/icon_building.svg").pixmap(18, 18));
        deptIcon->setStyleSheet("border:none;");
        QLabel* deptLabel = new QLabel("查看部门:");
        deptLabel->setStyleSheet("font-size:13px; font-weight:bold; color:#4E5969; border:none;");
        toolLay->addWidget(deptIcon);
        toolLay->addWidget(deptLabel);
        m_deptCombo = new QComboBox();
        m_deptCombo->addItem("全部");
        // 请求服务端获取所有部门名称
        QJsonObject dReq; dReq["type"] = "query_dept_list";
        QJsonObject dRes = NetworkHelper::request(dReq);
        QJsonArray depts = dRes["departments"].toArray();
        qDebug() << "[HomeModule] dept list size=" << depts.size();
        for (int i = 0; i < depts.size(); i++) m_deptCombo->addItem(depts[i].toString());
        m_deptCombo->setCurrentText(m_filterDept);
        m_deptCombo->setStyleSheet(
            "QComboBox { border:1px solid #DCDFE6; border-radius:4px; padding:4px 10px; "
            "min-width:140px; background:white; color:#303133; font-size:13px; }"
            "QComboBox:hover { border-color:#409EFF; }"
            "QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:25px; border-left:none; }"
            "QComboBox::down-arrow { image:none; border-left:5px solid transparent; border-right:5px solid transparent; border-top:5px solid #909399; }"
        );
        connect(m_deptCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            m_filterDept = text;
            refreshDashboard();
            });
        toolLay->addWidget(m_deptCombo);
    }
    toolLay->addStretch();
    // 刷新按钮，点击后重新加载仪表盘
    QPushButton* refreshBtn = new QPushButton(" 刷新");
    refreshBtn->setIcon(QIcon(iconBase + "Home/btn_refresh.svg"));
    refreshBtn->setIconSize(QSize(16, 16));
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setStyleSheet("QPushButton { background:#165DFF; color:white; border-radius:4px; padding:6px 16px; font-weight:bold; } QPushButton:hover { background:#4080FF; }");
    connect(refreshBtn, &QPushButton::clicked, this, &HomeModule::refreshDashboard);
    toolLay->addWidget(refreshBtn);
    parentLayout->addLayout(toolLay);
}
// 渲染顶部核心数据卡片：出勤率、到勤人数、异常人数、审批待办或个人异常
void HomeModule::renderTopCards(QVBoxLayout* parentLayout, const QJsonObject& data) {
    QHBoxLayout* cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(15);
    int totalExpected = data["total_expected"].toInt();
    int actualPunched = data["actual_punched"].toInt();
    int abnormalCount = data["abnormal_count"].toInt();   // 异常人数（不含请假）
    int leaveCount = data["leave_count"].toInt();          // 请假人数（独立统计）
    // 使用服务端预计算的出勤率
    QString rateStr = data["attendance_rate"].toString("0%");
    cardsLayout->addWidget(createDataCard("今日出勤率", rateStr, "较昨日持平", "#00B42A"));
    cardsLayout->addWidget(createDataCard("实到 / 应到人数", QString("%1 / %2").arg(actualPunched).arg(totalExpected), "人脸核验打卡", "#3370FF"));
    // 异常卡片仅统计迟到/早退/旷工，请假人数单独标注
    cardsLayout->addWidget(createDataCard("今日异常人数", QString::number(abnormalCount) + " 人",
        QString("迟到/早退/旷工 (请假%1人)").arg(leaveCount), "#F53F3F"));
    // 管理员或经理显示审批待办卡片，普通员工显示本月个人异常卡片
    if (m_role.contains("管理员") || m_role == "经理" || m_jobTitle == "部门经理") {
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
// 创建单个数据卡片，包含标题、数值和副标题
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
// 渲染中间图表区域：饼图、柱状图、折线图，标题随时间维度动态变化
void HomeModule::renderMiddleCharts(QVBoxLayout* parentLayout, const QJsonObject& res) {
    QHBoxLayout* chartsLayout = new QHBoxLayout();
    chartsLayout->setSpacing(15);
    chartsLayout->addWidget(createPieChart(res["pie_chart"].toArray()), 1);
    chartsLayout->addWidget(createBarChart(res["bar_chart"].toArray(),
        QString("数据排名 (%1)").arg(m_timeRange)), 1);
    chartsLayout->addWidget(createLineChart(res["line_chart"].toArray(),
        QString("出勤趋势 (%1)").arg(m_timeRange)), 1);
    parentLayout->addLayout(chartsLayout, 3);
}
// 创建饼图，按出勤状态分类着色显示占比
QChartView* HomeModule::createPieChart(const QJsonArray& data) {
    QChart* chart = new QChart();
    QPieSeries* series = new QPieSeries();
    for (int i = 0; i < data.size(); ++i) {
        QJsonObject o = data[i].toObject();
        series->append(o["status"].toString() + " (" + QString::number(o["count"].toInt()) + ")", o["count"].toInt());
    }
    if (series->count() == 0) series->append("暂无数据", 1);
    // 按状态关键字设置扇区颜色：正常绿、迟到早退红、请假橙、其他紫
    for (auto slice : series->slices()) {
        slice->setLabelVisible(true);
        if (slice->label().contains("正常")) slice->setBrush(QColor("#00B42A"));
        else if (slice->label().contains("迟到") || slice->label().contains("早退")) slice->setBrush(QColor("#F53F3F"));
        else if (slice->label().contains("请假") || slice->label().contains("假-")) slice->setBrush(QColor("#FF7D00"));
        else slice->setBrush(QColor("#722ED1"));
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
// 创建柱状图，展示各部门异常人次排名
QChartView* HomeModule::createBarChart(const QJsonArray& data, const QString& title) {
    QChart* chart = new QChart();
    QBarSeries* series = new QBarSeries();
    series->setLabelsVisible(true);                          // 柱顶显示数值
    series->setLabelsPosition(QAbstractBarSeries::LabelsOutsideEnd);
    QBarSet* setAbnormal = new QBarSet("异常人次");
    setAbnormal->setColor(QColor("#3370FF"));
    QStringList categories;
    for (int i = 0; i < data.size(); ++i) {
        QJsonObject o = data[i].toObject();
        categories << o["dept"].toString();
        *setAbnormal << o["count"].toInt();
    }
    if (categories.isEmpty()) { categories << "本人"; *setAbnormal << 0; }
    series->append(setAbnormal);
    chart->addSeries(series);
    chart->setTitle(title);
    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(categories);
    axisX->setLabelsAngle(-45);                              // X轴标签倾斜防重叠
    chart->addAxis(axisX, Qt::AlignBottom); series->attachAxis(axisX);
    QValueAxis* axisY = new QValueAxis(); axisY->setLabelFormat("%d");
    chart->addAxis(axisY, Qt::AlignLeft); series->attachAxis(axisY);
    chart->setMargins(QMargins(0, 0, 0, 0));
    chart->legend()->hide();
    // 鼠标悬停柱子时弹出提示，显示部门名称和数值
    QObject::connect(series, &QBarSeries::hovered, [chart](bool status, int index, QBarSet* set) {
        if (status) {
            QBarCategoryAxis* ax = qobject_cast<QBarCategoryAxis*>(chart->axes(Qt::Horizontal).first());
            QString dept = (ax && index < ax->categories().size()) ? ax->categories().at(index) : "未知";
            QToolTip::showText(QCursor::pos(), QString("%1: %2 人次").arg(dept).arg((int)set->at(index)));
        }
        else {
            QToolTip::hideText();
        }
        });
    QChartView* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setStyleSheet("background-color: white; border-radius: 8px; border: 1px solid #E5E6EB;");
    return view;
}
// 创建折线图，展示每日出勤趋势变化
QChartView* HomeModule::createLineChart(const QJsonArray& data, const QString& title) {
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
    chart->setTitle(title);
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
// 渲染底部区域：左侧实时打卡动态列表，右侧系统公告和快捷操作按钮
void HomeModule::renderBottomFeed(QVBoxLayout* parentLayout, const QJsonObject& res) {
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(15);
    QFrame* feedFrame = new QFrame();
    feedFrame->setStyleSheet("background-color: white; border-radius: 8px; border: 1px solid #E5E6EB;");
    QVBoxLayout* feedLay = new QVBoxLayout(feedFrame);
    QLabel* feedTitle = new QLabel("实时打卡动态");
    feedTitle->setStyleSheet("font-weight: bold; font-size: 14px; border: none;");
    QListWidget* feedList = new QListWidget();
    feedList->setStyleSheet("border: none; background: transparent; font-size: 13px;");
    // 遍历打卡动态列表，按状态设置不同颜色：正常绿、请假橙、异常红
    QJsonArray feedArr = res["feed_list"].toArray();
    for (int i = 0; i < feedArr.size(); ++i) {
        QJsonObject o = feedArr[i].toObject();
        QString st = o["status"].toString();
        QString iconFile = st.contains("正常") ? "Home/dot_green.svg" : (st.contains("假") ? "Home/dot_yellow.svg" : "Home/dot_red.svg");
        QString dotLabel = st.contains("正常") ? "●" : (st.contains("假") ? "●" : "●");
        QString dotColor = st.contains("正常") ? "#00B42A" : (st.contains("假") ? "#FF7D00" : "#F53F3F");
        feedList->addItem(QString("[%1] %2 %3 %4").arg(o["time"].toString(), o["name"].toString(), dotLabel, st));
        if (feedList->count() > 0) {
            QListWidgetItem* lastItem = feedList->item(feedList->count() - 1);
            lastItem->setForeground(QColor(dotColor));
        }
    }
    if (feedList->count() == 0) feedList->addItem("今日暂无打卡动态...");
    feedLay->addWidget(feedTitle);
    feedLay->addWidget(feedList);
    bottomLayout->addWidget(feedFrame, 2);
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
        noticeList->addItem(QString("[%1] %2").arg(o["date"].toString(), o["content"].toString()));
    }
    if (noticeList->count() == 0) noticeList->addItem("暂无最新公告...");
    rightLay->addWidget(noticeTitle);
    rightLay->addWidget(noticeList);
    // 底部操作按钮：普通员工显示请假和申诉，管理者显示审批按钮
    QHBoxLayout* btnLay = new QHBoxLayout();
    bool isManager = m_role.contains("管理员") || m_role == "经理" || m_jobTitle == "部门经理";
    if (!isManager) {
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
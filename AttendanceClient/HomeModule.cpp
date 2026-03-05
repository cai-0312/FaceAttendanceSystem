#include "HomeModule.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

HomeModule::HomeModule(QVBoxLayout* pieLayout, QVBoxLayout* barLayout, QVBoxLayout* lineLayout, QObject* parent)
    : QObject(parent), m_pieLayout(pieLayout), m_barLayout(barLayout), m_lineLayout(lineLayout)
{
}

HomeModule::~HomeModule()
{
}

void HomeModule::refreshDashboard()
{
    renderPieChart();
    renderBarChart();
    renderLineChart();
}

void HomeModule::clearLayout(QLayout* layout)
{
    if (!layout) return;
    QLayoutItem* child;
    while ((child = layout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }
}

void HomeModule::renderPieChart()
{
    QChart* pieChart = new QChart();
    QPieSeries* pieSeries = new QPieSeries();

    // 查询今天的出勤状态分布
    QSqlQuery pieQuery("SELECT status, COUNT(*) FROM attendance_records WHERE DATE(punch_time) = CURDATE() GROUP BY status");
    while (pieQuery.next()) {
        QString status = pieQuery.value(0).toString();
        int count = pieQuery.value(1).toInt();
        pieSeries->append(status + QString(" (%1)").arg(count), count);
    }

    // 若暂无数据时的兜底显示
    if (pieSeries->count() == 0) {
        pieSeries->append("暂无数据 (0)", 1);
        pieSeries->slices().first()->setBrush(QColor("#E5E6EB"));
    }
    else {
        // 美化切片颜色
        for (auto slice : pieSeries->slices()) {
            slice->setLabelVisible(true);
            if (slice->label().contains("正常")) slice->setBrush(QColor("#00B42A")); // 绿色
            else if (slice->label().contains("迟到") || slice->label().contains("异常")) slice->setBrush(QColor("#F53F3F")); // 红色
            else if (slice->label().contains("请假")) slice->setBrush(QColor("#FF7D00")); // 橙色
        }
    }

    pieChart->addSeries(pieSeries);
    pieChart->setTitle("今日实时出勤占比");
    pieChart->legend()->hide(); // 隐藏底部图例，让切片文本自己显示
    pieChart->setMargins(QMargins(10, 10, 10, 10));

    QChartView* pieView = new QChartView(pieChart);
    pieView->setRenderHint(QPainter::Antialiasing); // 开启抗锯齿

    clearLayout(m_pieLayout);
    m_pieLayout->addWidget(pieView);
}

void HomeModule::renderBarChart()
{
    QChart* barChart = new QChart();
    QBarSeries* barSeries = new QBarSeries();
    QBarSet* setAbnormal = new QBarSet("考勤异常次数");
    setAbnormal->setColor(QColor("#3370FF")); // 蓝色柱体

    QStringList categories;

    // 联合查询 users 表和考勤记录表，统计近 30 天各部门异常次数
    QSqlQuery barQuery("SELECT u.department, COUNT(a.id) "
        "FROM users u "
        "LEFT JOIN attendance_records a ON u.name = a.name AND a.status != '正常' "
        "WHERE DATE(a.punch_time) >= DATE_SUB(CURDATE(), INTERVAL 30 DAY) "
        "GROUP BY u.department");

    bool hasData = false;
    while (barQuery.next()) {
        hasData = true;
        QString dept = barQuery.value(0).toString();
        if (dept.isEmpty()) dept = "未分组";
        categories << dept;
        *setAbnormal << barQuery.value(1).toInt();
    }

    if (!hasData) {
        categories << "暂无";
        *setAbnormal << 0;
    }

    barSeries->append(setAbnormal);
    barChart->addSeries(barSeries);
    barChart->setTitle("近30天各部门考勤异常预警");

    // 配置 X 轴
    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(categories);
    barChart->addAxis(axisX, Qt::AlignBottom);
    barSeries->attachAxis(axisX);

    // 配置 Y 轴
    QValueAxis* axisY = new QValueAxis();
    axisY->setLabelFormat("%d");
    barChart->addAxis(axisY, Qt::AlignLeft);
    barSeries->attachAxis(axisY);

    barChart->setMargins(QMargins(10, 10, 10, 10));
    QChartView* barView = new QChartView(barChart);
    barView->setRenderHint(QPainter::Antialiasing);

    clearLayout(m_barLayout);
    m_barLayout->addWidget(barView);
}

void HomeModule::renderLineChart()
{
    QChart* lineChart = new QChart();
    QLineSeries* lineSeries = new QLineSeries();
    lineSeries->setName("实际打卡总人数");

    QPen pen(QColor("#F53F3F")); // 红色折线
    pen.setWidth(3);
    lineSeries->setPen(pen);

    // 查询最近 7 天的每日出勤人数（按人名去重）
    QSqlQuery lineQuery("SELECT DATE_FORMAT(punch_time, '%m-%d'), COUNT(DISTINCT name) "
        "FROM attendance_records "
        "WHERE punch_time >= DATE_SUB(CURDATE(), INTERVAL 6 DAY) "
        "GROUP BY DATE(punch_time) ORDER BY DATE(punch_time) ASC");

    QStringList days;
    int index = 0;
    while (lineQuery.next()) {
        days << lineQuery.value(0).toString();
        lineSeries->append(index++, lineQuery.value(1).toInt());
    }

    // 兜底补全
    if (days.isEmpty()) {
        days << "暂无记录";
        lineSeries->append(0, 0);
    }

    lineChart->addSeries(lineSeries);
    lineChart->setTitle("近7日全员出勤热度趋势");

    // 配置 X 轴
    QBarCategoryAxis* lineAxisX = new QBarCategoryAxis();
    lineAxisX->append(days);
    lineChart->addAxis(lineAxisX, Qt::AlignBottom);
    lineSeries->attachAxis(lineAxisX);

    // 配置 Y 轴
    QValueAxis* lineAxisY = new QValueAxis();
    lineAxisY->setLabelFormat("%d");
    lineChart->addAxis(lineAxisY, Qt::AlignLeft);
    lineSeries->attachAxis(lineAxisY);

    lineChart->setMargins(QMargins(10, 10, 10, 10));
    QChartView* lineView = new QChartView(lineChart);
    lineView->setRenderHint(QPainter::Antialiasing);

    clearLayout(m_lineLayout);
    m_lineLayout->addWidget(lineView);
}
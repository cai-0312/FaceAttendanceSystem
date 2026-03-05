#include "RecordModule.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QMap>
#include <QStringList>

RecordModule::RecordModule(QTableView* tableView, QCalendarWidget* calendar,
    QLabel* summaryLabel, QLabel* detailDateLabel,
    QLineEdit* searchNameEdit, QPushButton* filterBtn, QPushButton* exportBtn,
    QString loginName, QString role, QObject* parent)
    : QObject(parent), m_tableView(tableView), m_calendarWidget(calendar),
    m_summaryLabel(summaryLabel), m_detailDateLabel(detailDateLabel),
    m_searchNameEdit(searchNameEdit), m_filterBtn(filterBtn), m_exportBtn(exportBtn),
    m_loginName(loginName), m_role(role)
{
    // 1. 初始化表格模型
    m_model = new QSqlTableModel(this);
    m_model->setTable("attendance_records");
    m_tableView->setModel(m_model);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // 2. 权限控制：如果不是管理员，隐藏搜索别人的输入框和搜索按钮
    if (m_role != "管理员" && m_role != "超级管理员") {
        m_searchNameEdit->hide();
        m_filterBtn->hide();
    }

    // 3. 绑定日历事件
    connect(m_calendarWidget, &QCalendarWidget::clicked, this, &RecordModule::onCalendarClicked);
    connect(m_calendarWidget, &QCalendarWidget::currentPageChanged, this, &RecordModule::onCalendarPageChanged);

    // 4. 绑定其他按钮
    if (m_filterBtn) connect(m_filterBtn, &QPushButton::clicked, this, &RecordModule::onFilterClicked);
    if (m_exportBtn) connect(m_exportBtn, &QPushButton::clicked, this, &RecordModule::onExportClicked);

    // 5. 初始化载入当月数据
    m_currentSelectedDate = QDate::currentDate();
    m_calendarWidget->setSelectedDate(m_currentSelectedDate);
    loadMonthlyDataAndColorize(m_currentSelectedDate.year(), m_currentSelectedDate.month());
    onCalendarClicked(m_currentSelectedDate); // 默认显示今天的明细
}

// 外部通知刷新
void RecordModule::refreshData() {
    loadMonthlyDataAndColorize(m_currentSelectedDate.year(), m_currentSelectedDate.month());
    onFilterClicked();
}

// 🚀 核心一：加载本月考勤并给日历上色 (修复了重复计次 Bug)
void RecordModule::loadMonthlyDataAndColorize(int year, int month) {
    // 先清除日历上所有的特殊格式
    m_calendarWidget->setDateTextFormat(QDate(), QTextCharFormat());

    QTextCharFormat normalFormat, lateFormat, leaveFormat;

    // 🟢 正常打卡：浅绿底，深绿字
    normalFormat.setBackground(QColor("#F0F9EB"));
    normalFormat.setForeground(QColor("#00B42A"));
    normalFormat.setFontWeight(QFont::Bold);

    // 🔴 异常打卡：浅红底，深红字
    lateFormat.setBackground(QColor("#FEF0F0"));
    lateFormat.setForeground(QColor("#F53F3F"));
    lateFormat.setFontWeight(QFont::Bold);

    // 🟡 请假状态：浅黄底，深黄字
    leaveFormat.setBackground(QColor("#FDF6EC"));
    leaveFormat.setForeground(QColor("#FA6400"));
    leaveFormat.setFontWeight(QFont::Bold);

    QSqlQuery query;
    // 日历和看板永远只统计【自己】的数据
    query.prepare("SELECT DATE(punch_time), status FROM attendance_records "
        "WHERE name = :name AND YEAR(punch_time) = :y AND MONTH(punch_time) = :m");
    query.bindValue(":name", m_loginName);
    query.bindValue(":y", year);
    query.bindValue(":m", month);

    // ★ 核心修复：用 Map 按天把状态归类，避免一天多次打卡导致出勤天数暴涨！
    QMap<QDate, QStringList> dailyStatus;

    if (query.exec()) {
        while (query.next()) {
            QDate pDate = query.value(0).toDate();
            QString status = query.value(1).toString();
            dailyStatus[pDate].append(status);
        }
    }

    int normalDays = 0, lateDays = 0, absentDays = 0, leaveDays = 0;

    // 遍历每一天，评估当天的最终考勤状态
    for (auto it = dailyStatus.constBegin(); it != dailyStatus.constEnd(); ++it) {
        QDate pDate = it.key();
        QStringList statuses = it.value();

        bool hasLeave = false;
        bool hasAbsent = false;
        bool hasLate = false;
        bool hasNormal = false;

        for (const QString& s : statuses) {
            if (s.contains("假") || s.contains("调休")) hasLeave = true;
            else if (s.contains("旷工") || s.contains("作弊")) hasAbsent = true;
            else if (s.contains("迟到") || s.contains("早退")) hasLate = true;
            else if (s.contains("正常")) hasNormal = true;
        }

        // 按恶劣程度优先级进行涂色和天数统计（同一天不管打卡多少次，只算 1 天）
        if (hasLeave) {
            m_calendarWidget->setDateTextFormat(pDate, leaveFormat);
            leaveDays++;
        }
        else if (hasAbsent) {
            m_calendarWidget->setDateTextFormat(pDate, lateFormat);
            absentDays++;
        }
        else if (hasLate) {
            m_calendarWidget->setDateTextFormat(pDate, lateFormat);
            lateDays++;
        }
        else if (hasNormal) {
            m_calendarWidget->setDateTextFormat(pDate, normalFormat);
            normalDays++;
        }
    }

    // 更新顶部的【月度简报 Dashboard】
    QString summaryHtml = QString("出勤: <b><font color='#00B42A'>%1 天</font></b> &nbsp;&nbsp;|&nbsp;&nbsp; "
        "迟到/早退: <b><font color='#FA6400'>%2 天</font></b> &nbsp;&nbsp;|&nbsp;&nbsp; "
        "请假: <b><font color='#3370FF'>%3 天</font></b> &nbsp;&nbsp;|&nbsp;&nbsp; "
        "旷工: <b><font color='#F53F3F'>%4 天</font></b>")
        .arg(normalDays).arg(lateDays).arg(leaveDays).arg(absentDays);
    m_summaryLabel->setText(summaryHtml);
}

void RecordModule::onCalendarPageChanged(int year, int month) {
    loadMonthlyDataAndColorize(year, month);
}

void RecordModule::onCalendarClicked(const QDate& date) {
    m_currentSelectedDate = date;
    m_detailDateLabel->setText("📅 " + date.toString("yyyy年MM月dd日") + " 打卡明细");
    onFilterClicked();
}

// 🚀 核心二：安全查询、表头汉化与 ID 隐藏
void RecordModule::onFilterClicked() {
    QString filterStr = QString("DATE(punch_time) = '%1'").arg(m_currentSelectedDate.toString("yyyy-MM-dd"));

    // ★ 物理级防越权：如果是普通员工，强制绑定自己的名字
    if (m_role != "管理员" && m_role != "超级管理员") {
        filterStr += QString(" AND name = '%1'").arg(m_loginName);
    }
    // ★ 如果是管理员，允许通过输入框检索对应姓名的员工
    else {
        QString searchName = m_searchNameEdit->text().trimmed();
        if (!searchName.isEmpty()) {
            filterStr += QString(" AND name LIKE '%%1%'").arg(searchName);
        }
    }

    m_model->setFilter(filterStr);
    m_model->select(); // 先 select() 从数据库拉取结构，后面才能重命名表头！

    // ★ 1. 汉化表头
    m_model->setHeaderData(m_model->fieldIndex("name"), Qt::Horizontal, "员工姓名");
    m_model->setHeaderData(m_model->fieldIndex("punch_time"), Qt::Horizontal, "打卡时间");
    m_model->setHeaderData(m_model->fieldIndex("status"), Qt::Horizontal, "考勤状态");

    // ★ 2. 隐藏 id 这一列
    int idIndex = m_model->fieldIndex("id");
    if (idIndex != -1) {
        m_tableView->hideColumn(idIndex);
    }
}

// 导出 CSV 文件
void RecordModule::onExportClicked() {
    QString filePath = QFileDialog::getSaveFileName(nullptr, "导出考勤报表", "Attendance_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv", "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, "错误", "无法创建文件！");
        return;
    }

    QTextStream out(&file);
    // 写入 BOM 确保 Excel 打开不会中文乱码
    out.setEncoding(QStringConverter::Utf8);
    out << QString("\xEF\xBB\xBF");
    out << QStringLiteral("员工姓名,打卡时间,考勤状态\n");

    // 导出时也屏蔽 id 列，只导出我们需要的核心数据
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString name = m_model->record(i).value("name").toString();
        QString time = m_model->record(i).value("punch_time").toDateTime().toString("yyyy-MM-dd HH:mm:ss");
        QString status = m_model->record(i).value("status").toString();

        out << QString("%1,%2,%3\n").arg(name, time, status);
    }

    file.close();
    QMessageBox::information(nullptr, "成功", "考勤明细已成功导出为 Excel/CSV 格式！");
}
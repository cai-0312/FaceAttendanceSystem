#include "RecordModule.h"
#include "NetworkHelper.h" // 🚀 引入加固后的网络引擎
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QMap>
#include <QStringList>
#include <QBoxLayout>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>

RecordModule::RecordModule(QTableView* tableView, QCalendarWidget* calendar,
    QLabel* summaryLabel, QLabel* detailDateLabel,
    QLineEdit* searchNameEdit, QPushButton* filterBtn, QPushButton* exportBtn,
    QString loginName, QString role, QObject* parent)
    : QObject(parent), m_tableView(tableView), m_calendarWidget(calendar),
    m_summaryLabel(summaryLabel), m_detailDateLabel(detailDateLabel),
    m_searchNameEdit(searchNameEdit), m_filterBtn(filterBtn), m_exportBtn(exportBtn),
    m_loginName(loginName), m_role(role)
{
    m_tableModel = new QStandardItemModel(this);
    m_tableView->setModel(m_tableModel);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &RecordModule::onCustomContextMenu);

    injectAdvancedUI();

    if (m_role != "管理员登录" && m_role != "超级管理员" && m_role != "经理") {
        m_searchNameEdit->hide();
        m_filterBtn->hide();
    }

    connect(m_calendarWidget, &QCalendarWidget::clicked, this, &RecordModule::onCalendarClicked);
    connect(m_calendarWidget, &QCalendarWidget::currentPageChanged, this, &RecordModule::onCalendarPageChanged);
    if (m_filterBtn) connect(m_filterBtn, &QPushButton::clicked, this, &RecordModule::onFilterClicked);
    if (m_exportBtn) connect(m_exportBtn, &QPushButton::clicked, this, &RecordModule::onExportClicked);

    m_currentSelectedDate = QDate::currentDate();
    m_calendarWidget->setSelectedDate(m_currentSelectedDate);
    loadMonthlyDataAndColorize(m_currentSelectedDate.year(), m_currentSelectedDate.month());
    onCalendarClicked(m_currentSelectedDate);
}

void RecordModule::injectAdvancedUI() {
    QString comboQss = "QComboBox { border: 1px solid #DCDFE6; border-radius: 15px; padding: 0px 15px; background: white; color: #333333; }"
        "QComboBox::drop-down { border: none; width: 30px; }"
        "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 6px solid #333333; width: 0px; height: 0px; margin-right: 10px; }";

    QString dateQss = "QDateEdit { border: 1px solid #DCDFE6; border-radius: 15px; padding: 0px 10px; background: white; color: #333333; }"
        "QDateEdit::drop-down { border: none; width: 30px; }"
        "QDateEdit::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 6px solid #333333; width: 0px; height: 0px; margin-right: 10px; }";

    m_statusCombo = new QComboBox();
    m_statusCombo->addItems({ "全部状态", "正常", "迟到", "早退", "旷工", "请假", "补卡" });
    m_statusCombo->setMinimumHeight(32);
    m_statusCombo->setCursor(Qt::PointingHandCursor);
    m_statusCombo->setStyleSheet(comboQss);

    m_startDateEdit = new QDateEdit(QDate::currentDate());
    m_endDateEdit = new QDateEdit(QDate::currentDate());
    m_startDateEdit->setCalendarPopup(true);
    m_endDateEdit->setCalendarPopup(true);
    m_startDateEdit->setMinimumHeight(32);
    m_endDateEdit->setMinimumHeight(32);
    m_startDateEdit->setStyleSheet(dateQss);
    m_endDateEdit->setStyleSheet(dateQss);

    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setContentsMargins(0, 10, 0, 10);
    filterLayout->addWidget(new QLabel("区 间:"));
    filterLayout->addWidget(m_startDateEdit);
    filterLayout->addWidget(new QLabel("-"));
    filterLayout->addWidget(m_endDateEdit);
    filterLayout->addSpacing(15);
    filterLayout->addWidget(new QLabel("状 态:"));
    filterLayout->addWidget(m_statusCombo);
    filterLayout->addStretch();

    if (QWidget* parentW = m_tableView->parentWidget()) {
        if (QVBoxLayout* lay = qobject_cast<QVBoxLayout*>(parentW->layout())) {
            int tableIndex = lay->indexOf(m_tableView);
            lay->insertLayout(tableIndex, filterLayout);
        }
    }

    m_exportMonthBtn = new QPushButton("导出月度汇总");
    m_exportMonthBtn->setMinimumHeight(m_exportBtn->minimumHeight());
    m_exportMonthBtn->setCursor(Qt::PointingHandCursor);
    m_exportMonthBtn->setStyleSheet("QPushButton { background-color: #FF7D00; color: white; border: none; border-radius: 6px; font-weight: bold; padding: 0 15px; } QPushButton:hover { background-color: #FF9A2E; }");

    QPushButton* myRequestsBtn = new QPushButton("我的申请进度");
    myRequestsBtn->setMinimumHeight(m_exportBtn->minimumHeight());
    myRequestsBtn->setCursor(Qt::PointingHandCursor);
    myRequestsBtn->setStyleSheet("QPushButton { background-color: #409EFF; color: white; border: none; border-radius: 6px; font-weight: bold; padding: 0 15px; } QPushButton:hover { background-color: #66B1FF; }");

    if (QWidget* parentE = m_exportBtn->parentWidget()) {
        if (QBoxLayout* lay = qobject_cast<QBoxLayout*>(parentE->layout())) {
            int idx = lay->indexOf(m_exportBtn);
            lay->insertWidget(idx, m_exportMonthBtn);
            lay->insertWidget(idx, myRequestsBtn);
        }
    }

    if (m_role != "管理员登录" && m_role != "超级管理员" && m_role != "经理") {
        m_exportMonthBtn->hide();
    }

    connect(m_statusCombo, &QComboBox::currentTextChanged, this, &RecordModule::onFilterClicked);
    connect(m_startDateEdit, &QDateEdit::dateChanged, this, &RecordModule::onFilterClicked);
    connect(m_endDateEdit, &QDateEdit::dateChanged, this, &RecordModule::onFilterClicked);
    connect(m_exportMonthBtn, &QPushButton::clicked, this, &RecordModule::onExportMonthClicked);

    // 请求我的进度数据
    connect(myRequestsBtn, &QPushButton::clicked, this, [this]() {
        QDialog dlg(m_tableView->window());
        dlg.setWindowTitle(QString("我的流程申请记录 (%1)").arg(m_loginName));
        dlg.resize(750, 450);

        QVBoxLayout* layout = new QVBoxLayout(&dlg);
        QTabWidget* tabWidget = new QTabWidget(&dlg);

        QTableWidget* leaveTable = new QTableWidget();
        leaveTable->setColumnCount(6);
        leaveTable->setHorizontalHeaderLabels({ "请假类型", "开始时间", "结束时间", "请假理由", "当前审批人", "状态" });
        leaveTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        leaveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        leaveTable->setSelectionBehavior(QAbstractItemView::SelectRows);

        QTableWidget* appealTable = new QTableWidget();
        appealTable->setColumnCount(5);
        appealTable->setHorizontalHeaderLabels({ "异常时间", "申诉类型", "申诉理由", "当前审批人", "状态" });
        appealTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        appealTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        appealTable->setSelectionBehavior(QAbstractItemView::SelectRows);

        QJsonObject req;
        req["type"] = "query_my_requests";
        req["name"] = m_loginName.trimmed();
        // 🚀 核心替换：使用 NetworkHelper
        QJsonObject res = NetworkHelper::request(req);

        if (res.isEmpty() || res["status"].toString() != "success") {
            QMessageBox::warning(&dlg, "网络异常", "无法从服务器获取申请进度，请检查服务端是否正常运行。");
        }
        else {
            QJsonArray leaveArr = res["leave_data"].toArray();
            for (int i = 0; i < leaveArr.size(); ++i) {
                QJsonObject rowObj = leaveArr[i].toObject();
                int row = leaveTable->rowCount();
                leaveTable->insertRow(row);
                leaveTable->setItem(row, 0, new QTableWidgetItem(rowObj["type"].toString()));
                leaveTable->setItem(row, 1, new QTableWidgetItem(rowObj["start"].toString()));
                leaveTable->setItem(row, 2, new QTableWidgetItem(rowObj["end"].toString()));
                leaveTable->setItem(row, 3, new QTableWidgetItem(rowObj["reason"].toString()));
                leaveTable->setItem(row, 4, new QTableWidgetItem(rowObj["approver"].toString()));

                QTableWidgetItem* statusItem = new QTableWidgetItem(rowObj["status"].toString());
                if (rowObj["status"].toString() == "已批准") {
                    statusItem->setForeground(QBrush(QColor("#67C23A")));
                    statusItem->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
                }
                else if (rowObj["status"].toString() == "已拒绝") {
                    statusItem->setForeground(QBrush(QColor("#F56C6C")));
                }
                else {
                    statusItem->setForeground(QBrush(QColor("#E6A23C")));
                }
                leaveTable->setItem(row, 5, statusItem);
            }

            QJsonArray appealArr = res["appeal_data"].toArray();
            for (int i = 0; i < appealArr.size(); ++i) {
                QJsonObject rowObj = appealArr[i].toObject();
                int row = appealTable->rowCount();
                appealTable->insertRow(row);
                appealTable->setItem(row, 0, new QTableWidgetItem(rowObj["time"].toString()));
                appealTable->setItem(row, 1, new QTableWidgetItem(rowObj["type"].toString()));
                appealTable->setItem(row, 2, new QTableWidgetItem(rowObj["reason"].toString()));
                appealTable->setItem(row, 3, new QTableWidgetItem(rowObj["approver"].toString()));

                QTableWidgetItem* statusItem = new QTableWidgetItem(rowObj["status"].toString());
                if (rowObj["status"].toString() == "已批准") {
                    statusItem->setForeground(QBrush(QColor("#67C23A")));
                    statusItem->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
                }
                else {
                    statusItem->setForeground(QBrush(QColor("#E6A23C")));
                }
                appealTable->setItem(row, 4, statusItem);
            }
        }

        tabWidget->addTab(leaveTable, "我的请假记录");
        tabWidget->addTab(appealTable, "我的申诉记录");
        layout->addWidget(tabWidget);
        dlg.exec();
        });
}

void RecordModule::refreshData() {
    loadMonthlyDataAndColorize(m_currentSelectedDate.year(), m_currentSelectedDate.month());
    onFilterClicked();
}

void RecordModule::loadMonthlyDataAndColorize(int year, int month) {
    m_calendarWidget->setDateTextFormat(QDate(), QTextCharFormat());
    QTextCharFormat normalF, lateF, leaveF;
    normalF.setBackground(QColor("#F0F9EB")); normalF.setForeground(QColor("#00B42A")); normalF.setFontWeight(QFont::Bold);
    lateF.setBackground(QColor("#FEF0F0")); lateF.setForeground(QColor("#F53F3F")); lateF.setFontWeight(QFont::Bold);
    leaveF.setBackground(QColor("#FDF6EC")); leaveF.setForeground(QColor("#FA6400")); leaveF.setFontWeight(QFont::Bold);

    QJsonObject req;
    req["type"] = "query_monthly_status";
    req["name"] = m_loginName.trimmed();
    req["year"] = year;
    req["month"] = month;
    // 🚀 核心替换：使用 NetworkHelper
    QJsonObject res = NetworkHelper::request(req);

    if (!res.isEmpty()) {
        int normalDays = res["normal_days"].toInt();
        int lateDays = res["late_days"].toInt();
        int leaveDays = res["leave_days"].toInt();
        int absentDays = res["absent_days"].toInt();

        QJsonObject colorMap = res["color_map"].toObject();
        for (auto it = colorMap.begin(); it != colorMap.end(); ++it) {
            QDate d = QDate::fromString(it.key(), "yyyy-MM-dd");
            QString state = it.value().toString();
            if (state == "leave") m_calendarWidget->setDateTextFormat(d, leaveF);
            else if (state == "late" || state == "absent") m_calendarWidget->setDateTextFormat(d, lateF);
            else if (state == "normal") m_calendarWidget->setDateTextFormat(d, normalF);
        }

        m_summaryLabel->setText(QString("出勤: <b><font color='#00B42A'>%1 天</font></b> &nbsp;&nbsp;|&nbsp;&nbsp; 迟到/早退: <b><font color='#FA6400'>%2 天</font></b> &nbsp;&nbsp;|&nbsp;&nbsp; 请假: <b><font color='#3370FF'>%3 天</font></b> &nbsp;&nbsp;|&nbsp;&nbsp; 旷工: <b><font color='#F53F3F'>%4 天</font></b>").arg(normalDays).arg(lateDays).arg(leaveDays).arg(absentDays));
    }
}

void RecordModule::onCalendarPageChanged(int year, int month) {
    loadMonthlyDataAndColorize(year, month);
}

void RecordModule::onCalendarClicked(const QDate& date) {
    m_currentSelectedDate = date;
    m_startDateEdit->blockSignals(true); m_endDateEdit->blockSignals(true);
    m_startDateEdit->setDate(date); m_endDateEdit->setDate(date);
    m_startDateEdit->blockSignals(false); m_endDateEdit->blockSignals(false);

    m_detailDateLabel->setText(QString(" %1 至 %2 记录").arg(m_startDateEdit->date().toString("MM月dd日"), m_endDateEdit->date().toString("MM月dd日")));
    onFilterClicked();
}

void RecordModule::onFilterClicked() {
    m_detailDateLabel->setText(QString(" %1 至 %2 记录").arg(m_startDateEdit->date().toString("MM月dd日"), m_endDateEdit->date().toString("MM月dd日")));

    m_tableModel->clear();
    m_tableModel->setHorizontalHeaderLabels({ "隐藏ID", "员工姓名", "打卡时间", "考勤状态", "来源" });

    QString nameFilter = (m_role != "管理员登录" && m_role != "超级管理员") ? m_loginName.trimmed() : m_searchNameEdit->text().trimmed();

    QJsonObject req;
    req["type"] = "query_attendance_detail";
    req["name_filter"] = nameFilter;
    req["start_date"] = m_startDateEdit->date().toString("yyyy-MM-dd");
    req["end_date"] = m_endDateEdit->date().toString("yyyy-MM-dd");
    req["status_filter"] = m_statusCombo->currentText();

    // 🚀 核心替换：使用 NetworkHelper
    QJsonObject res = NetworkHelper::request(req);

    if (res["status"].toString() == "success") {
        QJsonArray records = res["records"].toArray();
        for (int i = 0; i < records.size(); ++i) {
            QJsonObject rowObj = records[i].toObject();
            QList<QStandardItem*> rowItems;
            rowItems << new QStandardItem(rowObj["id"].toString());
            rowItems << new QStandardItem(rowObj["name"].toString());
            rowItems << new QStandardItem(rowObj["time"].toString());

            QString st = rowObj["status"].toString();
            QStandardItem* statusItem = new QStandardItem(st);
            if (st.contains("假")) {
                statusItem->setForeground(QBrush(QColor("#FA6400")));
                statusItem->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
            }
            rowItems << statusItem;
            rowItems << new QStandardItem(rowObj["source"].toString());

            for (auto item : rowItems) item->setTextAlignment(Qt::AlignCenter);
            m_tableModel->appendRow(rowItems);
        }
    }

    m_tableView->hideColumn(0);
    m_tableView->hideColumn(4);
}

void RecordModule::onCustomContextMenu(const QPoint& pos) {
    QModelIndex index = m_tableView->indexAt(pos);
    if (!index.isValid()) return;

    int row = index.row();
    QString recordId = m_tableModel->item(row, 0)->text();
    QString empName = m_tableModel->item(row, 1)->text();
    QString timeStr = m_tableModel->item(row, 2)->text();
    QString status = m_tableModel->item(row, 3)->text();
    QString source = m_tableModel->item(row, 4)->text();

    QMenu menu(m_tableView);

    if (empName == m_loginName && !status.contains("正常") && !status.contains("假")) {
        QAction* appealAct = menu.addAction("对此异常记录发起申诉");
        connect(appealAct, &QAction::triggered, [=]() {
            QMessageBox::information(nullptr, "快捷申诉", QString("请前往【首页大屏】点击【异常申诉】。\n您要申诉的记录为：%1 [%2]").arg(timeStr, status));
            });
    }

    if (m_role == "管理员登录" || m_role == "超级管理员") {
        if (source == "L") {
            QAction* disableAct = menu.addAction("⚠️ 审批通过的假单请勿在此强行修改");
            disableAct->setEnabled(false);
        }
        else {
            QAction* editAct = menu.addAction("管理员：强制修改状态 / 补卡");
            connect(editAct, &QAction::triggered, [=]() {
                bool ok;
                QStringList items = { "正常(补卡)", "迟到", "早退", "旷工" };
                QString newStatus = QInputDialog::getItem(nullptr, "修改考勤状态", QString("正在修改【%1】于 %2 的记录：").arg(empName, timeStr), items, 0, false, &ok);
                if (ok && !newStatus.isEmpty()) {
                    QJsonObject req;
                    req["type"] = "admin_modify_status";
                    req["record_id"] = recordId.toInt();
                    req["new_status"] = newStatus;

                    // 🚀 核心替换：使用 NetworkHelper
                    NetworkHelper::request(req);

                    QMessageBox::information(nullptr, "成功", "修改指令已下发！");
                    refreshData();
                }
                });
        }
    }

    if (!menu.actions().isEmpty()) {
        menu.exec(m_tableView->viewport()->mapToGlobal(pos));
    }
}

void RecordModule::onExportClicked() {
    QString filePath = QFileDialog::getSaveFileName(nullptr, "导出流水账", "Detail_" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".csv", "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file); out.setEncoding(QStringConverter::Utf8); out << QString("\xEF\xBB\xBF") << "员工姓名,打卡时间,考勤状态\n";

    for (int i = 0; i < m_tableModel->rowCount(); ++i) {
        out << QString("%1,%2,%3\n").arg(
            m_tableModel->item(i, 1)->text(),
            m_tableModel->item(i, 2)->text(),
            m_tableModel->item(i, 3)->text());
    }
    file.close();
    QMessageBox::information(nullptr, "成功", "明细流水账已导出！");
}

void RecordModule::onExportMonthClicked() {
    QMessageBox::information(nullptr, "提示", "为保证数据安全，月度统计请联系管理员在服务端总控中心导出！");
}
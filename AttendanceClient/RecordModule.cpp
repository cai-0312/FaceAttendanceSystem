#include "RecordModule.h"
#include "NetworkHelper.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
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
// 构造函数：执行考勤记录模块的基础初始化，并基于角色对数据列和查询权限进行控制
RecordModule::RecordModule(QTableView* tableView, QCalendarWidget* calendar,
    QLabel* summaryLabel, QLabel* detailDateLabel,
    QLineEdit* searchNameEdit, QPushButton* filterBtn, QPushButton* exportBtn,
    QString loginName, QString role, QObject* parent)
    : QObject(parent), m_tableView(tableView), m_calendarWidget(calendar),
    m_summaryLabel(summaryLabel), m_detailDateLabel(detailDateLabel),
    m_searchNameEdit(searchNameEdit), m_filterBtn(filterBtn), m_exportBtn(exportBtn),
    m_loginName(loginName), m_role(role)
{
    // 配置考勤明细底层数据模型及视图只读交互策略
    m_tableModel = new QStandardItemModel(this);
    m_tableView->setModel(m_tableModel);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // 启用自定义右键菜单策略以支持管理员对异常打卡流水的高级操作
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &RecordModule::onCustomContextMenu);
    // 动态注入时间段与高级状态过滤面板
    injectAdvancedUI();
    // 细粒度权限控制：普通职员视角强制隐藏跨部门及姓名筛选按钮
    if (m_role != "管理员登录" && m_role != "经理") {
        m_searchNameEdit->hide();
        m_filterBtn->hide();
    }
    // 绑定交互控件底层信号
    connect(m_calendarWidget, &QCalendarWidget::clicked, this, &RecordModule::onCalendarClicked);
    connect(m_calendarWidget, &QCalendarWidget::currentPageChanged, this, &RecordModule::onCalendarPageChanged);
    if (m_filterBtn) connect(m_filterBtn, &QPushButton::clicked, this, &RecordModule::onFilterClicked);
    if (m_exportBtn) connect(m_exportBtn, &QPushButton::clicked, this, &RecordModule::onExportPersonal);
    // 初始化时主动同步当前日期的考勤面板及色彩日历矩阵
    m_currentSelectedDate = QDate::currentDate();
    m_calendarWidget->setSelectedDate(m_currentSelectedDate);
    loadMonthlyDataAndColorize(m_currentSelectedDate.year(), m_currentSelectedDate.month());
    QString currentStyle = m_calendarWidget->styleSheet();
    m_calendarWidget->setStyleSheet(currentStyle + " QCalendarWidget QAbstractItemView { border: 1px solid #E0E4E8; }");
    onCalendarClicked(m_currentSelectedDate);
}
// 动态UI构建：为老旧界面的容器拓展时段筛选控件与请假/申诉工作台导流按钮
void RecordModule::injectAdvancedUI() {
    QString comboQss = "QComboBox { border: 1px solid #DCDFE6; border-radius: 15px; padding: 0px 15px; background: white; color: #333333; }"
        "QComboBox::drop-down { border: none; width: 30px; }"
        "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 6px solid #333333; width: 0px; height: 0px; margin-right: 10px; }";

    QString dateQss = "QDateEdit { border: 1px solid #DCDFE6; border-radius: 15px; padding: 0px 10px; background: white; color: #333333; }"
        "QDateEdit::drop-down { border: none; width: 30px; }"
        "QDateEdit::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 6px solid #333333; width: 0px; height: 0px; margin-right: 10px; }";
    // 构造高级状态过滤下拉框
    m_statusCombo = new QComboBox();
    m_statusCombo->addItems({ "全部状态", "正常", "迟到", "早退", "旷工","作弊", "请假", "补卡" });
    m_statusCombo->setMinimumHeight(32);
    m_statusCombo->setCursor(Qt::PointingHandCursor);
    m_statusCombo->setStyleSheet(comboQss);
    // 构造具备日历弹窗功能的时间区间选择器
    m_startDateEdit = new QDateEdit(QDate::currentDate());
    m_endDateEdit = new QDateEdit(QDate::currentDate());
    m_startDateEdit->setCalendarPopup(true);
    m_endDateEdit->setCalendarPopup(true);
    m_startDateEdit->setMinimumHeight(32);
    m_endDateEdit->setMinimumHeight(32);
    m_startDateEdit->setStyleSheet(dateQss);
    m_endDateEdit->setStyleSheet(dateQss);
    // 组合高级筛选布局区域
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
    // 探测主表格所在容器层并实施布局级侵入插入
    if (QWidget* parentW = m_tableView->parentWidget()) {
        if (QVBoxLayout* lay = qobject_cast<QVBoxLayout*>(parentW->layout())) {
            int tableIndex = lay->indexOf(m_tableView);
            lay->insertLayout(tableIndex, filterLayout);
        }
    }
    // 构造“导出中心”下拉菜单父按钮
    QString iconBase = "../../AttendanceClient/icon_library/";
    m_exportCenterBtn = new QPushButton(" 导出中心");
    m_exportCenterBtn->setIcon(QIcon(iconBase + "Record/btn_export.svg"));
    m_exportCenterBtn->setIconSize(QSize(18, 18));
    m_exportCenterBtn->setMinimumHeight(36);
    m_exportCenterBtn->setCursor(Qt::PointingHandCursor);
    m_exportCenterBtn->setStyleSheet(
        "QPushButton { background-color: #00B42A; color: white; border-radius: 6px; font-weight: bold; padding: 0 15px; }"
        "QPushButton::menu-indicator { image: none; }" // 隐藏默认的小箭头，保持UI整洁
        "QPushButton:hover { background-color: #23C343; }"
    );

    // 实例化原生 QMenu 以保证多分辨率下自动规避屏幕边缘遮挡
    QMenu* exportMenu = new QMenu(m_exportCenterBtn);
    exportMenu->setStyleSheet(
        "QMenu { background-color: white; border: 1px solid #DEE0E3; border-radius: 6px; padding: 4px; box-shadow: 0 2px 12px 0 rgba(0,0,0,0.1); }"
        "QMenu::item { padding: 10px 25px; color: #1F2329; font-weight: bold; border-radius: 4px; }"
        "QMenu::item:selected { background-color: #F2F3F5; color: #3370FF; }"
    );

    QAction* actExportPersonal = new QAction(QIcon(iconBase + "Record/btn_export_personal.svg"), "导出个人明细", this);
    QAction* actExportDept = new QAction(QIcon(iconBase + "Record/btn_export_dept.svg"), "导出部门报表", this);
    QAction* actExportAll = new QAction(QIcon(iconBase + "Record/btn_export_all.svg"), "导出全员月度汇总", this);

    exportMenu->addAction(actExportPersonal);

    // 权限控制：按层级动态挂载菜单项
    if (m_role == "管理员登录" || m_role == "经理") {
        exportMenu->addAction(actExportDept);
    }
    if (m_role == "管理员登录") {
        exportMenu->addAction(actExportAll);
    }

    m_exportCenterBtn->setMenu(exportMenu);

    // 挂载原有的“我的申请进度”按钮逻辑（保持不变）
    QPushButton* myRequestsBtn = new QPushButton("我的申请进度");
    myRequestsBtn->setMinimumHeight(36);
    myRequestsBtn->setCursor(Qt::PointingHandCursor);
    myRequestsBtn->setStyleSheet("QPushButton { background-color: #409EFF; color: white; border: none; border-radius: 6px; font-weight: bold; padding: 0 15px; } QPushButton:hover { background-color: #66B1FF; }");

    // 将新控件挂载至底部操作横向流布局中 (替换掉原来的 m_exportBtn 等)
    if (QWidget* parentE = m_exportBtn->parentWidget()) {
        if (QBoxLayout* lay = qobject_cast<QBoxLayout*>(parentE->layout())) {
            int idx = lay->indexOf(m_exportBtn);
            // 移除或隐藏旧按钮
            m_exportBtn->hide();
            lay->insertWidget(idx, m_exportCenterBtn);
            lay->insertWidget(idx + 1, myRequestsBtn);
        }
    }

    // 绑定信号槽
    connect(actExportPersonal, &QAction::triggered, this, &RecordModule::onExportPersonal);
    connect(actExportDept, &QAction::triggered, this, &RecordModule::onExportDept);
    connect(actExportAll, &QAction::triggered, this, &RecordModule::onExportAllMonthly);
    // 发起网络请求获取当前用户所发起的请假与申诉流转审批进度
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
        QJsonObject res = NetworkHelper::request(req);
        if (res.isEmpty() || res["status"].toString() != "success") {
            QMessageBox::warning(&dlg, "网络异常", "无法从服务器获取申请进度，请检查服务端是否正常运行。");
        }
        else {
            // 解析请假流程数据模型并向表格控件中按行投递
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
            // 解析申诉流程数据模型并向表格控件中按行投递
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
void RecordModule::onExportDept() {
    QString startDate = m_startDateEdit->date().toString("yyyy-MM-dd");
    QString endDate = m_endDateEdit->date().toString("yyyy-MM-dd");

    if (m_startDateEdit->date() > m_endDateEdit->date()) {
        QMessageBox::warning(nullptr, "时间错误", "起始日期不能晚于结束日期！");
        return;
    }

    QJsonObject req;
    req["type"] = "query_dept_summary";
    req["name"] = m_loginName;
    req["role"] = m_role;
    req["start_date"] = startDate;
    req["end_date"] = endDate;

    QJsonObject res = NetworkHelper::request(req);
    if (res.isEmpty() || res["status"].toString() != "success") {
        QMessageBox::warning(nullptr, "请求拦截或失败", res["msg"].toString("无法获取部门报表数据，可能由于权限不足或网络异常。"));
        return;
    }

    QJsonArray deptArr = res["data"].toArray();
    if (deptArr.isEmpty()) {
        QMessageBox::information(nullptr, "无数据", "所选时间区间内无相关部门考勤数据。");
        return;
    }

    QString defaultName = QString("部门考勤宏观报表_%1至%2.csv").arg(startDate, endDate);
    QString filePath = QFileDialog::getSaveFileName(nullptr, "保存部门报表", defaultName, "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QString("\xEF\xBB\xBF"); // UTF-8 BOM 头，防止 Excel 乱码

    // 需求 2 & 5：严格对齐的表头维度
    out << QString("【部门级考勤宏观聚合报表】\n");
    out << QString("统计区间: %1 至 %2\n").arg(startDate, endDate);
    out << QString("导出人: %1 (%2)\n\n").arg(m_loginName, m_role);

    out << "部门名称,总计人数,应出勤总人天,迟到总计(次),早退总计(次),作弊总计(次),旷工总计(天),请假总计(天),异常打卡占比,总计扣薪工时(h),部门平均出勤率\n";

    for (int i = 0; i < deptArr.size(); ++i) {
        QJsonObject row = deptArr[i].toObject();
        out << QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11\n")
            .arg(row["dept_name"].toString())
            .arg(row["total_people"].toInt())
            .arg(row["expected_mandays"].toInt())
            .arg(row["total_late"].toInt())
            .arg(row["total_early"].toInt())
            .arg(row["total_cheat"].toInt())
            .arg(row["total_absent"].toInt())
            .arg(row["total_leave"].toInt())
            .arg(row["abnormal_rate"].toString())
            .arg(row["deduct_hours"].toDouble(), 0, 'f', 1)
            .arg(row["attendance_rate"].toString());
    }

    file.close();
    QMessageBox::information(nullptr, "导出成功", "部门考勤报表导出完毕！");
}
// 供外部父容器主动调度的数据刷新接口
void RecordModule::refreshData() {
    loadMonthlyDataAndColorize(m_currentSelectedDate.year(), m_currentSelectedDate.month());
    onFilterClicked();
}
// 向服务端发包获取用户当月的所有考勤行为切片，并利用 Qt 底层颜色引擎对日历控件执行宏观绘图
void RecordModule::loadMonthlyDataAndColorize(int year, int month) {
    m_calendarWidget->setDateTextFormat(QDate(), QTextCharFormat());
    QTextCharFormat normalF, lateF, leaveF;
    // 初始化各类考勤状态的渲染笔刷参数
    normalF.setBackground(QColor("#F0F9EB")); normalF.setForeground(QColor("#00B42A")); normalF.setFontWeight(QFont::Bold);
    lateF.setBackground(QColor("#FEF0F0")); lateF.setForeground(QColor("#F53F3F")); lateF.setFontWeight(QFont::Bold);
    leaveF.setBackground(QColor("#FDF6EC")); leaveF.setForeground(QColor("#FA6400")); leaveF.setFontWeight(QFont::Bold);
    QJsonObject req;
    req["type"] = "query_monthly_status";
    req["name"] = m_loginName.trimmed();
    req["year"] = year;
    req["month"] = month;
    QJsonObject res = NetworkHelper::request(req);
    if (!res.isEmpty()) {
        int normalDays = res["normal_days"].toInt();
        int lateDays = res["late_days"].toInt();
        int leaveDays = res["leave_days"].toInt();
        int absentDays = res["absent_days"].toInt();
        // 依据哈希表透传的数据枚举对每一天的格子背景实施重绘染色
        QJsonObject colorMap = res["color_map"].toObject();
        for (auto it = colorMap.begin(); it != colorMap.end(); ++it) {
            QDate d = QDate::fromString(it.key(), "yyyy-MM-dd");
            QString state = it.value().toString();
            if (state == "leave") m_calendarWidget->setDateTextFormat(d, leaveF);
            else if (state == "late" || state == "absent") m_calendarWidget->setDateTextFormat(d, lateF);
            else if (state == "normal") m_calendarWidget->setDateTextFormat(d, normalF);
        }
        m_summaryLabel->setText(QString("出勤: <b><font color='#00B42A'>%1 天</font></b> &nbsp;&nbsp;|&nbsp;&nbsp; 迟到/早退/作弊: <b><font color='#FA6400'>%2 天</font></b> &nbsp;&nbsp;|&nbsp;&nbsp; 请假: <b><font color='#3370FF'>%3 天</font></b> &nbsp;&nbsp;|&nbsp;&nbsp; 旷工: <b><font color='#F53F3F'>%4 天</font></b>").arg(normalDays).arg(lateDays).arg(leaveDays).arg(absentDays));
    }
}
// 挂钩日历翻页组件，当展示月份变更时主动发包重拉最新数据集
void RecordModule::onCalendarPageChanged(int year, int month) {
    loadMonthlyDataAndColorize(year, month);
}
// 用户针对指定日期实施精细点击时触发检索条件联动重置
void RecordModule::onCalendarClicked(const QDate& date) {
    m_currentSelectedDate = date;
    // 屏蔽信号发射以避免更新日期时重复进入无限死循环请求
    m_startDateEdit->blockSignals(true);
    m_endDateEdit->blockSignals(true);
    m_startDateEdit->setDate(date);
    m_endDateEdit->setDate(date);
    m_startDateEdit->blockSignals(false);
    m_endDateEdit->blockSignals(false);
    m_detailDateLabel->setText(QString(" %1 至 %2 记录").arg(m_startDateEdit->date().toString("MM月dd日"), m_endDateEdit->date().toString("MM月dd日")));
    onFilterClicked();
}
// 发起核心综合条件查询请求：聚合起始结束日期、状态枚举和人员名称获取明细表并渲染至界面表格
void RecordModule::onFilterClicked() {
    m_detailDateLabel->setText(QString(" %1 至 %2 记录").arg(m_startDateEdit->date().toString("MM月dd日"), m_endDateEdit->date().toString("MM月dd日")));
    m_tableModel->clear();
    m_tableModel->setHorizontalHeaderLabels({ "隐藏ID", "员工姓名", "打卡时间", "考勤状态", "来源" });
    // 管理员与普通职员分别使用搜索输入框的值或固定账户名以实现硬级别的脱敏鉴权
    QString nameFilter = (m_role != "管理员登录") ? m_loginName.trimmed() : m_searchNameEdit->text().trimmed();
    QJsonObject req;
    req["type"] = "query_attendance_detail";
    req["name_filter"] = nameFilter;
    req["start_date"] = m_startDateEdit->date().toString("yyyy-MM-dd");
    req["end_date"] = m_endDateEdit->date().toString("yyyy-MM-dd");
    req["status_filter"] = m_statusCombo->currentText();
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
            if (st.contains("迟到") || st.contains("早退")) {
                statusItem->setForeground(QBrush(QColor("#FA6400")));
                statusItem->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
            }
            else if (st.contains("旷工") || st.contains("作弊")) {
                statusItem->setForeground(QBrush(QColor("#F53F3F")));
                statusItem->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
            }
            else if (st.contains("假")) {
                statusItem->setForeground(QBrush(QColor("#3370FF")));
                statusItem->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
            }
            else if (st.contains("正常") || st.contains("补卡")) {
                statusItem->setForeground(QBrush(QColor("#00B42A")));
            }
            rowItems << statusItem;
            rowItems << new QStandardItem(rowObj["source"].toString());
            for (auto item : rowItems) item->setTextAlignment(Qt::AlignCenter);
            m_tableModel->appendRow(rowItems);
        }
    }
    // 后台列数据包含ID及来源标识，在此作界面隐藏但不销毁数据模型引用
    m_tableView->hideColumn(0);
    m_tableView->hideColumn(4);
}
// 捕获表格容器内置的鼠标右键坐标偏移量以映射呈现不同角色的业务操作上下文菜单
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
    // 对于普通用户发现异常考勤时，在右侧辅助引流提供申诉通道挂点
    if (empName == m_loginName && !status.contains("正常") && !status.contains("假")) {
        QAction* appealAct = menu.addAction("对此异常记录发起申诉");
        connect(appealAct, &QAction::triggered, [=]() {
            QMessageBox::information(nullptr, "快捷申诉", QString("请前往【异常申诉】。\n您要申诉的记录为：%1 [%2]").arg(timeStr, status));
            });
    }
    // 针对管理员阶层提供更底层的权限，允许直接对考勤数据库实施硬级别的状态字段覆写修改
    if (m_role == "管理员登录") {
        if (source == "L") {
            // 防御机制：如该考勤记录状态属于请假流转通过变更所得，则将其置灰禁止二次覆盖
            QAction* disableAct = menu.addAction("⚠️ 审批通过的假单请勿在此强行修改");
            disableAct->setEnabled(false);
        }
        else {
            QAction* editAct = menu.addAction("管理员：强制修改状态 / 补卡");
            connect(editAct, &QAction::triggered, [=]() {
                bool ok;
                QStringList items = { "正常(补卡)", "迟到", "早退", "旷工" , "作弊打卡" };
                QString newStatus = QInputDialog::getItem(nullptr, "修改考勤状态", QString("正在修改【%1】于 %2 的记录：").arg(empName, timeStr), items, 0, false, &ok);
                if (ok && !newStatus.isEmpty()) {
                    QJsonObject req;
                    req["type"] = "admin_modify_status";
                    req["record_id"] = recordId.toInt();
                    req["new_status"] = newStatus;

                    NetworkHelper::request(req);

                    QMessageBox::information(nullptr, "成功", "修改指令已下发！");
                    refreshData();
                }
                });
        }
    }
    // 将生成的行为动作面板悬浮覆盖于鼠标触发原点全局坐标上
    if (!menu.actions().isEmpty()) {
        menu.exec(m_tableView->viewport()->mapToGlobal(pos));
    }
}
// 获取当前挂载表格内已加载好的所有序列化明细矩阵，转储为纯文本格式流并输出至本地CSV文件
void RecordModule::onExportPersonal() {
    if (m_tableModel->rowCount() == 0) {
        QMessageBox::warning(nullptr, "提示", "当前表格没有数据，请先筛选出记录再导出。");
        return;
    }

    QString startStr = m_startDateEdit->date().toString("yyyyMMdd");
    QString endStr = m_endDateEdit->date().toString("yyyyMMdd");
    QString defaultName = QString("考勤明细_%1_%2_%3.csv").arg(m_loginName, startStr, endStr);

    QString filePath = QFileDialog::getSaveFileName(nullptr, "导出考勤明细", defaultName, "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(nullptr, "错误", "无法创建文件，请检查路径权限。");
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QString("\xEF\xBB\xBF");

    out << QString("考勤明细报表\n");
    out << QString("查询区间: %1 至 %2\n").arg(
        m_startDateEdit->date().toString("yyyy-MM-dd"),
        m_endDateEdit->date().toString("yyyy-MM-dd"));
    out << QString("筛选状态: %1\n").arg(m_statusCombo->currentText());
    out << QString("导出时间: %1\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    out << QString("共 %1 条记录\n\n").arg(m_tableModel->rowCount());

    out << "序号,员工姓名,打卡时间,考勤状态\n";
    for (int i = 0; i < m_tableModel->rowCount(); ++i) {
        out << QString("%1,%2,%3,%4\n").arg(
            QString::number(i + 1),
            m_tableModel->item(i, 1)->text(),
            m_tableModel->item(i, 2)->text(),
            m_tableModel->item(i, 3)->text());
    }

    file.close();
    QMessageBox::information(nullptr, "导出成功",
        QString("考勤明细已导出！\n共 %1 条记录\n文件：%2").arg(m_tableModel->rowCount()).arg(filePath));
}
// 大体量数据的宏观统计导出仅向具有全局总控调度权限的服务端管理系统开放以避免终端堵塞
void RecordModule::onExportAllMonthly() {
    if (m_role != "管理员登录") {
        QMessageBox::warning(nullptr, "权限不足", "仅管理员可导出月度汇总报表。");
        return;
    }

    int year = m_calendarWidget->yearShown();
    int month = m_calendarWidget->monthShown();

    QJsonObject req;
    req["type"] = "query_monthly_summary_all";
    req["year"] = year;
    req["month"] = month;

    QJsonObject res = NetworkHelper::request(req);
    if (res.isEmpty() || res["status"].toString() != "success") {
        QMessageBox::warning(nullptr, "请求失败", "无法从服务器获取月度汇总数据，请检查服务端是否正常运行。");
        return;
    }

    QJsonArray summaryArr = res["summary"].toArray();
    if (summaryArr.isEmpty()) {
        QMessageBox::information(nullptr, "提示", QString("%1年%2月暂无考勤数据。").arg(year).arg(month));
        return;
    }

    QString defaultName = QString("月度汇总_%1年%2月.csv").arg(year).arg(month, 2, 10, QChar('0'));
    QString filePath = QFileDialog::getSaveFileName(nullptr, "导出月度汇总", defaultName, "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(nullptr, "错误", "无法创建文件，请检查路径权限。");
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QString("\xEF\xBB\xBF");

    out << QString("月度考勤汇总报表\n");
    out << QString("统计月份: %1年%2月\n").arg(year).arg(month);
    out << QString("导出时间: %1\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    out << QString("导出人员: %1\n").arg(m_loginName);
    out << QString("共 %1 名员工\n\n").arg(summaryArr.size());

    out << "序号,员工姓名,部门,职务,应出勤(天),实出勤(天),迟到(次),早退(次),作弊(次),旷工(天),请假(天),出勤率\n";

    for (int i = 0; i < summaryArr.size(); ++i) {
        QJsonObject row = summaryArr[i].toObject();
        QString name = row["name"].toString();
        QString dept = row["dept"].toString();
        QString job = row["job_title"].toString();
        int shouldWork = row["should_work"].toInt();
        int actualWork = row["actual_work"].toInt();
        int lateCount = row["late_count"].toInt();
        int earlyCount = row["early_count"].toInt();
        int cheatCount = row["cheatCount"].toInt();
        int absentDays = row["absent_days"].toInt();
        int leaveDays = row["leave_days"].toInt();

        QString rate = shouldWork > 0
            ? QString::number((double)actualWork / shouldWork * 100.0, 'f', 1) + "%"
            : "0.0%";
        out << QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12\n")
            .arg(i + 1).arg(name).arg(dept).arg(job)
            .arg(shouldWork).arg(actualWork)
            .arg(lateCount).arg(earlyCount)
            .arg(cheatCount).arg(absentDays)
            .arg(leaveDays).arg(rate);
    }

    file.close();
    QMessageBox::information(nullptr, "导出成功",
        QString("%1年%2月,月度汇总已导出！")
        .arg(year).arg(month).arg(summaryArr.size()).arg(filePath));
}
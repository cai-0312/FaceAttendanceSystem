#include "PunchModule.h"
#include <QPixmap>
#include <QDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QTimeEdit>
#include <QSpinBox>
#include <QListWidget>
#include <QDateTimeEdit>
#include <QTextEdit>
#include <QTableWidget>
#include <QHeaderView>

PunchModule::PunchModule(QLabel* cameraLabel, QPushButton* manualBtn,
    QLabel* morningTime, QLabel* morningStatus,
    QLabel* eveningTime, QLabel* eveningStatus,
    QPushButton* ruleBtn, QPushButton* leaveReqBtn, QPushButton* leaveApprBtn, QLabel* currentTimeLabel,
    QString role, QString loginName, QObject* parent)
    : QObject(parent), m_cameraLabel(cameraLabel), m_manualBtn(manualBtn),
    m_lblMorningTime(morningTime), m_lblMorningStatus(morningStatus),
    m_lblEveningTime(eveningTime), m_lblEveningStatus(eveningStatus),
    m_btnRuleSettings(ruleBtn), m_btnLeaveRequest(leaveReqBtn), m_btnLeaveApprove(leaveApprBtn),
    m_lblCurrentTime(currentTimeLabel), m_role(role), m_loginName(loginName), m_cheatCount(0)
{
    initRulesTable();
    loadRules();
    loadTodayPunchStatus();

    if (m_manualBtn) connect(m_manualBtn, &QPushButton::clicked, this, &PunchModule::onManualPunchClicked);
    if (m_btnLeaveRequest) connect(m_btnLeaveRequest, &QPushButton::clicked, this, &PunchModule::onLeaveRequestClicked);
    if (m_btnLeaveApprove) connect(m_btnLeaveApprove, &QPushButton::clicked, this, &PunchModule::onLeaveApproveClicked);

    if (m_role != "管理员" && m_role != "超级管理员") {
        if (m_btnRuleSettings) m_btnRuleSettings->setHidden(true);
        if (m_btnLeaveApprove) m_btnLeaveApprove->setHidden(true);
    }
    else {
        if (m_btnRuleSettings) connect(m_btnRuleSettings, &QPushButton::clicked, this, &PunchModule::onRuleSettingsClicked);
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PunchModule::onTimeUpdate);
    m_timer->start(1000);
    onTimeUpdate();
}

void PunchModule::initRulesTable() {
    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS attendance_rules ("
        "id INT PRIMARY KEY, "
        "start_time TIME, end_time TIME, "
        "late_mins INT, absent_mins INT)");
    query.exec("INSERT IGNORE INTO attendance_rules (id, start_time, end_time, late_mins, absent_mins) "
        "VALUES (1, '09:00:00', '18:00:00', 30, 120)");

    query.exec("CREATE TABLE IF NOT EXISTS leave_requests ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "applicant VARCHAR(50), leave_type VARCHAR(50), "
        "start_time DATETIME, end_time DATETIME, "
        "duration VARCHAR(20), reason TEXT, "
        "approver VARCHAR(50), cc VARCHAR(50), status VARCHAR(20))");
}

void PunchModule::loadRules() {
    QSqlQuery query("SELECT start_time, end_time, late_mins, absent_mins FROM attendance_rules WHERE id = 1");
    if (query.next()) {
        m_startTime = query.value(0).toTime();
        m_endTime = query.value(1).toTime();
        m_lateMins = query.value(2).toInt();
        m_absentMins = query.value(3).toInt();
        if (m_lblMorningTime) m_lblMorningTime->setText("上班 " + m_startTime.toString("HH:mm"));
        if (m_lblEveningTime) m_lblEveningTime->setText("下班 " + m_endTime.toString("HH:mm"));
    }
}

void PunchModule::loadTodayPunchStatus() {
    if (m_loginName.isEmpty()) return;

    m_lblMorningStatus->setText("未打卡");
    m_lblMorningStatus->setStyleSheet("color: #909399;");
    m_lblEveningStatus->setText("未打卡");
    m_lblEveningStatus->setStyleSheet("color: #909399;");

    QSqlQuery query;
    query.prepare("SELECT punch_time, status FROM attendance_records "
        "WHERE name = :name AND DATE(punch_time) = CURDATE() ORDER BY punch_time ASC");
    query.bindValue(":name", m_loginName);

    if (query.exec()) {
        bool hasMorning = false;
        while (query.next()) {
            QTime pTime = query.value(0).toDateTime().time();
            QString status = query.value(1).toString();
            QString displayTxt = QString("🔵 %1 %2").arg(pTime.toString("HH:mm")).arg(status);

            if (pTime < QTime(12, 0) && !hasMorning) {
                m_lblMorningStatus->setText(displayTxt);
                m_lblMorningStatus->setStyleSheet(status.contains("迟到") || status.contains("旷工") ? "color: #F56C6C;" : "color: #67C23A;");
                hasMorning = true;
            }
            else {
                m_lblEveningStatus->setText(displayTxt + " (更新打卡)");
                m_lblEveningStatus->setStyleSheet(status.contains("早退") ? "color: #E6A23C;" : "color: #409EFF;");
            }
        }
    }
}

QString PunchModule::calculatePunchStatus(const QTime& punchTime) {
    if (punchTime < QTime(12, 0)) {
        int secsLate = m_startTime.secsTo(punchTime);
        if (secsLate <= 0) return "正常打卡";
        if (secsLate > m_absentMins * 60) return "旷工";
        if (secsLate > m_lateMins * 60) return "严重迟到";
        return "迟到";
    }
    else {
        int secsEarly = punchTime.secsTo(m_endTime);
        if (secsEarly <= 0) return "正常下班";
        return "早退";
    }
}

void PunchModule::onRuleSettingsClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("考勤规则设置");
    QFormLayout form(&dialog);

    QTimeEdit* startEdit = new QTimeEdit(m_startTime, &dialog);
    QTimeEdit* endEdit = new QTimeEdit(m_endTime, &dialog);
    QSpinBox* lateEdit = new QSpinBox(&dialog); lateEdit->setRange(0, 300); lateEdit->setValue(m_lateMins); lateEdit->setSuffix(" 分钟");
    QSpinBox* absentEdit = new QSpinBox(&dialog); absentEdit->setRange(0, 600); absentEdit->setValue(m_absentMins); absentEdit->setSuffix(" 分钟");

    form.addRow("上班时间:", startEdit);
    form.addRow("下班时间:", endEdit);
    form.addRow("判定迟到(超):", lateEdit);
    form.addRow("判定旷工(超):", absentEdit);

    QDialogButtonBox buttonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

    if (dialog.exec() == QDialog::Accepted) {
        QSqlQuery query;
        query.prepare("UPDATE attendance_rules SET start_time=:s, end_time=:e, late_mins=:l, absent_mins=:a WHERE id=1");
        query.bindValue(":s", startEdit->time().toString("HH:mm:ss"));
        query.bindValue(":e", endEdit->time().toString("HH:mm:ss"));
        query.bindValue(":l", lateEdit->value());
        query.bindValue(":a", absentEdit->value());
        if (query.exec()) {
            QMessageBox::information(nullptr, "成功", "考勤规则保存成功！");
            loadRules();
            loadTodayPunchStatus();
        }
    }
}

void PunchModule::onTimeUpdate() {
    if (m_lblCurrentTime) {
        m_lblCurrentTime->setText("当前时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    }
}

void PunchModule::renderFrame(const QImage& img) {
    if (m_cameraLabel) m_cameraLabel->setPixmap(QPixmap::fromImage(img).scaled(m_cameraLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void PunchModule::updateRecognizedName(const QString& name) {
    m_currentFaceName = name;
}

void PunchModule::onManualPunchClicked() {
    QDialog dialog((QWidget*)this->parent());
    QFormLayout form(&dialog);
    QLineEdit nameEdit(&dialog); QComboBox deptCombo(&dialog);
    deptCombo.addItems({ "总办", "研发部", "财务部", "人事行政部" });
    form.addRow("自报姓名:", &nameEdit); form.addRow("自报部门:", &deptCombo);
    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

    if (dialog.exec() == QDialog::Accepted) {
        QString claimName = nameEdit.text().trimmed();
        if (claimName == m_currentFaceName && !m_currentFaceName.isEmpty()) {
            m_cheatCount = 0;
            QDateTime now = QDateTime::currentDateTime();
            QString autoStatus = calculatePunchStatus(now.time());

            QSqlQuery query;
            query.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (:name, :time, :status)");
            query.bindValue(":name", claimName);
            query.bindValue(":time", now.toString("yyyy-MM-dd HH:mm:ss"));
            query.bindValue(":status", autoStatus);
            if (query.exec()) {
                QMessageBox::information(nullptr, "打卡成功", "人工辅助打卡成功！状态：" + autoStatus);
                loadTodayPunchStatus();
            }
        }
        else {
            m_cheatCount++;
            if (m_cheatCount >= 3) {
                QSqlQuery query;
                query.exec(QString("INSERT INTO attendance_records (name, punch_time, status) VALUES ('%1', NOW(), '作弊打卡')").arg(claimName));
                QMessageBox::critical(nullptr, "警告", "连续3次核验不符，已记为作弊！");
                m_cheatCount = 0;
                loadTodayPunchStatus();
            }
            else QMessageBox::warning(nullptr, "核验失败", "人脸与自报姓名不符！");
        }
    }
}

// 🚀 申请请假流程
void PunchModule::onLeaveRequestClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();

    QDialog typeDlg(parentWidget);
    typeDlg.setWindowTitle("请选择请假类型");
    typeDlg.resize(400, 500);
    QVBoxLayout* typeLayout = new QVBoxLayout(&typeDlg);
    QListWidget* typeList = new QListWidget(&typeDlg);
    typeList->setStyleSheet("QListWidget::item { height: 50px; border-bottom: 1px solid #EEE; font-size: 16px; }");
    QStringList types = { "事假 (按小时)", "调休 (按小时)", "病假 (按小时)", "年假 (按半天)", "产假 (按天)", "陪产假 (按天)", "婚假 (按天)", "例假 (按半天)", "丧假 (按天)", "哺乳假 (按小时)" };
    typeList->addItems(types);
    typeLayout->addWidget(typeList);

    QString selectedType = "";
    connect(typeList, &QListWidget::itemDoubleClicked, [&](QListWidgetItem* item) {
        selectedType = item->text().split(" ").first();
        typeDlg.accept();
        });
    if (typeDlg.exec() != QDialog::Accepted || selectedType.isEmpty()) return;

    QDialog formDlg(parentWidget);
    formDlg.setWindowTitle("发起请假提交 - " + selectedType);
    formDlg.resize(450, 600);
    QFormLayout* form = new QFormLayout(&formDlg);

    QDateTimeEdit* startEdit = new QDateTimeEdit(QDateTime::currentDateTime(), &formDlg);
    QDateTimeEdit* endEdit = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1), &formDlg);
    QLineEdit* durationEdit = new QLineEdit(&formDlg); durationEdit->setPlaceholderText("请输入时长...");
    QTextEdit* reasonEdit = new QTextEdit(&formDlg); reasonEdit->setPlaceholderText("请输入请假事由...");

    QComboBox* approverCombo = new QComboBox(&formDlg);
    QComboBox* ccCombo = new QComboBox(&formDlg);

    QSqlQuery query("SELECT name, role FROM users");
    while (query.next()) {
        QString n = query.value(0).toString();
        QString r = query.value(1).toString();
        if (n == m_loginName) continue;
        if (r == "管理员" || r == "超级管理员") approverCombo->addItem("👨‍💼 " + n, n);
        ccCombo->addItem("👤 " + n, n);
    }

    form->addRow("开始时间: *", startEdit);
    form->addRow("结束时间: *", endEdit);
    form->addRow("时长: *", durationEdit);
    form->addRow("请假事由:", reasonEdit);
    form->addRow("审批人: *", approverCombo);
    form->addRow("抄送人:", ccCombo);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &formDlg);
    buttonBox.button(QDialogButtonBox::Ok)->setText("提交申请");
    form->addRow(&buttonBox);
    connect(&buttonBox, &QDialogButtonBox::accepted, &formDlg, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &formDlg, &QDialog::reject);

    if (formDlg.exec() == QDialog::Accepted) {
        QString approver = approverCombo->currentData().toString();
        QString cc = ccCombo->currentData().toString();

        if (approver.isEmpty()) {
            QMessageBox::warning(parentWidget, "错误", "无可用管理员，无法发起审批！");
            return;
        }

        QSqlQuery insertQuery;
        insertQuery.prepare("INSERT INTO leave_requests (applicant, leave_type, start_time, end_time, duration, reason, approver, cc, status) "
            "VALUES (:ap, :ty, :st, :et, :du, :re, :app, :cc, '待审批')");
        insertQuery.bindValue(":ap", m_loginName); insertQuery.bindValue(":ty", selectedType);
        insertQuery.bindValue(":st", startEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss"));
        insertQuery.bindValue(":et", endEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss"));
        insertQuery.bindValue(":du", durationEdit->text()); insertQuery.bindValue(":re", reasonEdit->toPlainText());
        insertQuery.bindValue(":app", approver); insertQuery.bindValue(":cc", cc);
        insertQuery.exec();

        QString msgForApprover = QString("【OA审批请求】\n类型: %1\n时长: %2\n事由: %3\n请前往考勤页面的[审批请假]进行处理。")
            .arg(selectedType, durationEdit->text(), reasonEdit->toPlainText());
        emit requestSendChat(approver, msgForApprover);

        if (!cc.isEmpty()) {
            emit requestSendChat(cc, QString("【OA抄送通知】 %1 发起了 %2 申请，特此知会。").arg(m_loginName, selectedType));
        }

        QMessageBox::information(parentWidget, "成功", "请假申请已提交，并已通过内部通讯发送给审批人和抄送人！");
    }
}

// 🚀 审批请假流程 (修复了 C++ Lambda 复制对象报错的问题)
void PunchModule::onLeaveApproveClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();

    // ★ 核心修复：把局部变量改成动态分配的指针，绕过复制限制
    QDialog* apprDlg = new QDialog(parentWidget);
    apprDlg->setWindowTitle("我的审批待办");
    apprDlg->resize(800, 400);
    QVBoxLayout* layout = new QVBoxLayout(apprDlg);

    QTableWidget* table = new QTableWidget(apprDlg);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({ "申请人", "类型", "开始时间", "事由", "状态", "操作" });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    QSqlQuery query(QString("SELECT id, applicant, leave_type, start_time, reason FROM leave_requests WHERE approver='%1' AND status='待审批'").arg(m_loginName));
    while (query.next()) {
        int row = table->rowCount(); table->insertRow(row);
        int reqId = query.value(0).toInt();
        QString applicant = query.value(1).toString();
        QString lType = query.value(2).toString();
        QString sTime = query.value(3).toString();

        table->setItem(row, 0, new QTableWidgetItem(applicant));
        table->setItem(row, 1, new QTableWidgetItem(lType));
        table->setItem(row, 2, new QTableWidgetItem(sTime));
        table->setItem(row, 3, new QTableWidgetItem(query.value(4).toString()));
        table->setItem(row, 4, new QTableWidgetItem("🔴 待处理"));

        QPushButton* btnPass = new QPushButton("✅ 批准并记录考勤");
        btnPass->setStyleSheet("color: white; background-color: #67C23A;");
        table->setCellWidget(row, 5, btnPass);

        // ★ 这里的 [=] 会安全地复制 apprDlg 这个指针
        connect(btnPass, &QPushButton::clicked, [=]() {
            QSqlQuery updateQ;
            updateQ.exec(QString("UPDATE leave_requests SET status='已批准' WHERE id=%1").arg(reqId));

            updateQ.exec(QString("INSERT INTO attendance_records (name, punch_time, status) VALUES ('%1', '%2', '📝 %3')")
                .arg(applicant, sTime, lType));

            emit requestSendChat(applicant, QString("✅ 您的【%1】申请已通过！考勤流水已自动更新。").arg(lType));

            QMessageBox::information(apprDlg, "成功", "已批准并更新考勤！");
            apprDlg->accept();
            });
    }
    layout->addWidget(table);
    apprDlg->exec();
    apprDlg->deleteLater(); // 执行完后清理内存
}
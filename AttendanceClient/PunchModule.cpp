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
#include <QProcess>

PunchModule::PunchModule(QLabel* cameraLabel, QPushButton* manualBtn,
    QLabel* morningTime, QLabel* morningStatus,
    QLabel* eveningTime, QLabel* eveningStatus,
    QPushButton* ruleBtn, QPushButton* leaveReqBtn, QPushButton* leaveApprBtn,
    QPushButton* appealReqBtn, QPushButton* appealApprBtn,
    QLabel* currentTimeLabel,
    QString role, QString loginName, QObject* parent)
    : QObject(parent), m_cameraLabel(cameraLabel), m_manualBtn(manualBtn),
    m_lblMorningTime(morningTime), m_lblMorningStatus(morningStatus),
    m_lblEveningTime(eveningTime), m_lblEveningStatus(eveningStatus),
    m_btnRuleSettings(ruleBtn), m_btnLeaveRequest(leaveReqBtn), m_btnLeaveApprove(leaveApprBtn),
    m_btnAppealRequest(appealReqBtn), m_btnAppealApprove(appealApprBtn),
    m_lblCurrentTime(currentTimeLabel), m_role(role), m_loginName(loginName), m_cheatCount(0)
{
    initRulesTable();
    loadRules();
    loadTodayPunchStatus();

    if (m_manualBtn) connect(m_manualBtn, &QPushButton::clicked, this, &PunchModule::onManualPunchClicked);
    if (m_btnLeaveRequest) connect(m_btnLeaveRequest, &QPushButton::clicked, this, &PunchModule::onLeaveRequestClicked);
    if (m_btnLeaveApprove) connect(m_btnLeaveApprove, &QPushButton::clicked, this, &PunchModule::onLeaveApproveClicked);
    if (m_btnAppealRequest) connect(m_btnAppealRequest, &QPushButton::clicked, this, &PunchModule::onAppealRequestClicked);
    if (m_btnAppealApprove) connect(m_btnAppealApprove, &QPushButton::clicked, this, &PunchModule::onAppealApproveClicked);

    if (m_role != "管理员" && m_role != "超级管理员") {
        if (m_btnRuleSettings) m_btnRuleSettings->setHidden(true);
        if (m_btnLeaveApprove) m_btnLeaveApprove->setHidden(true);
        if (m_btnAppealApprove) m_btnAppealApprove->setHidden(true);
    }
    else {
        if (m_btnRuleSettings) connect(m_btnRuleSettings, &QPushButton::clicked, this, &PunchModule::onRuleSettingsClicked);
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PunchModule::onTimeUpdate);
    m_timer->start(1000);
    onTimeUpdate();
}

void PunchModule::speakText(const QString& text) {
    QString command = QString("Add-Type -AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer).Speak('%1');").arg(text);
    QProcess::startDetached("powershell", QStringList() << "-WindowStyle" << "Hidden" << "-Command" << command);
}

void PunchModule::initRulesTable() {
    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS shifts (name VARCHAR(50) PRIMARY KEY, start_time TIME, end_time TIME, late_mins INT, absent_mins INT)");
    query.exec("INSERT IGNORE INTO shifts (name, start_time, end_time, late_mins, absent_mins) VALUES ('常规班', '09:00:00', '18:00:00', 30, 120)");
    query.exec("INSERT IGNORE INTO shifts (name, start_time, end_time, late_mins, absent_mins) VALUES ('早班', '08:00:00', '17:00:00', 30, 120)");
    query.exec("INSERT IGNORE INTO shifts (name, start_time, end_time, late_mins, absent_mins) VALUES ('晚班', '13:00:00', '22:00:00', 30, 120)");
    query.exec("ALTER TABLE users ADD COLUMN shift_name VARCHAR(50) DEFAULT '常规班'");
    query.exec("CREATE TABLE IF NOT EXISTS appeals (id INT AUTO_INCREMENT PRIMARY KEY, applicant VARCHAR(50), abnormal_time DATETIME, original_status VARCHAR(50), reason TEXT, approver VARCHAR(50), status VARCHAR(20))");
    query.exec("CREATE TABLE IF NOT EXISTS leave_requests (id INT AUTO_INCREMENT PRIMARY KEY, applicant VARCHAR(50), leave_type VARCHAR(50), start_time DATETIME, end_time DATETIME, duration VARCHAR(20), reason TEXT, approver VARCHAR(50), cc VARCHAR(50), status VARCHAR(20))");
}

void PunchModule::loadRules() {
    QString sql = QString("SELECT shift_name FROM users WHERE name = '%1'").arg(m_loginName);
    QSqlQuery query(sql);
    if (query.next()) {
        m_shiftName = query.value(0).toString();
        if (m_shiftName.isEmpty()) m_shiftName = "常规班";
    }
    else { m_shiftName = "常规班"; }

    QString shiftSql = QString("SELECT start_time, end_time, late_mins, absent_mins FROM shifts WHERE name = '%1'").arg(m_shiftName);
    QSqlQuery shiftQuery(shiftSql);
    if (shiftQuery.next()) {
        m_startTime = shiftQuery.value(0).toTime();
        m_endTime = shiftQuery.value(1).toTime();
        m_lateMins = shiftQuery.value(2).toInt();
        m_absentMins = shiftQuery.value(3).toInt();
        if (m_lblMorningTime) m_lblMorningTime->setText(QString("[%1] 上班 %2").arg(m_shiftName, m_startTime.toString("HH:mm")));
        if (m_lblEveningTime) m_lblEveningTime->setText(QString("[%1] 下班 %2").arg(m_shiftName, m_endTime.toString("HH:mm")));
    }
}

void PunchModule::loadTodayPunchStatus() {
    if (m_loginName.isEmpty()) return;

    m_lblMorningStatus->setText("未打卡");
    m_lblMorningStatus->setStyleSheet("color: #909399;");
    m_lblEveningStatus->setText("未打卡");
    m_lblEveningStatus->setStyleSheet("color: #909399;");

    QString sql = QString("SELECT punch_time, status FROM attendance_records WHERE name = '%1' AND DATE(punch_time) = CURDATE() ORDER BY punch_time ASC").arg(m_loginName);
    QSqlQuery query(sql);

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
    dialog.setWindowTitle("多套排班制设置");
    QFormLayout form(&dialog);

    QComboBox* shiftCombo = new QComboBox(&dialog);
    shiftCombo->addItems({ "常规班", "早班", "晚班" });
    QTimeEdit* startEdit = new QTimeEdit(&dialog);
    QTimeEdit* endEdit = new QTimeEdit(&dialog);
    QSpinBox* lateEdit = new QSpinBox(&dialog); lateEdit->setRange(0, 300); lateEdit->setSuffix(" 分钟");
    QSpinBox* absentEdit = new QSpinBox(&dialog); absentEdit->setRange(0, 600); absentEdit->setSuffix(" 分钟");

    auto loadShiftData = [&](const QString& shift) {
        QSqlQuery q(QString("SELECT start_time, end_time, late_mins, absent_mins FROM shifts WHERE name='%1'").arg(shift));
        if (q.next()) {
            startEdit->setTime(q.value(0).toTime()); endEdit->setTime(q.value(1).toTime());
            lateEdit->setValue(q.value(2).toInt()); absentEdit->setValue(q.value(3).toInt());
        }
        };
    loadShiftData("常规班");
    connect(shiftCombo, &QComboBox::currentTextChanged, loadShiftData);

    form.addRow("选择排班:", shiftCombo);
    form.addRow("上班时间:", startEdit);
    form.addRow("下班时间:", endEdit);
    form.addRow("判定迟到(超):", lateEdit);
    form.addRow("判定旷工(超):", absentEdit);

    QDialogButtonBox buttonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

    if (dialog.exec() == QDialog::Accepted) {
        QString sql = QString("UPDATE shifts SET start_time='%1', end_time='%2', late_mins=%3, absent_mins=%4 WHERE name='%5'")
            .arg(startEdit->time().toString("HH:mm:ss")).arg(endEdit->time().toString("HH:mm:ss"))
            .arg(lateEdit->value()).arg(absentEdit->value()).arg(shiftCombo->currentText());
        QSqlQuery query;
        if (query.exec(sql)) {
            QMessageBox::information(nullptr, "成功", shiftCombo->currentText() + " 规则已更新！");
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

            QString sql = QString("INSERT INTO attendance_records (name, punch_time, status) VALUES ('%1', '%2', '%3')")
                .arg(claimName).arg(now.toString("yyyy-MM-dd HH:mm:ss")).arg(autoStatus);
            QSqlQuery query;
            if (query.exec(sql)) {
                QString greeting = (now.time() < QTime(12, 0)) ? "早上好" : "下午好";
                speakText(QString("打卡成功！%1，%2！当前状态：%3").arg(claimName, greeting, autoStatus));
                QMessageBox::information(nullptr, "打卡成功", "记录已生成！状态：" + autoStatus);
                loadTodayPunchStatus();
            }
        }
        else {
            m_cheatCount++;
            if (m_cheatCount >= 3) {
                QString sql = QString("INSERT INTO attendance_records (name, punch_time, status) VALUES ('%1', NOW(), '作弊打卡')").arg(claimName);
                QSqlQuery query(sql);
                speakText("警告！人脸核验不符，已记录异常！");
                QMessageBox::critical(nullptr, "警告", "连续3次核验不符，已记为作弊！");
                m_cheatCount = 0;
                loadTodayPunchStatus();
            }
            else QMessageBox::warning(nullptr, "核验失败", "人脸与自报姓名不符！");
        }
    }
}

void PunchModule::onAppealRequestClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();
    QDialog formDlg(parentWidget);
    formDlg.setWindowTitle("发起异常考勤申诉");
    formDlg.resize(450, 400);
    QFormLayout* form = new QFormLayout(&formDlg);

    QComboBox* recordCombo = new QComboBox(&formDlg);
    QTextEdit* reasonEdit = new QTextEdit(&formDlg); reasonEdit->setPlaceholderText("请输入申诉理由（如：拜访客户未打卡）...");
    QComboBox* approverCombo = new QComboBox(&formDlg);

    QString sqlAppeals = QString("SELECT punch_time, status FROM attendance_records WHERE name='%1' AND "
        "(status LIKE '%%迟到%%' OR status LIKE '%%早退%%' OR status LIKE '%%旷工%%' OR status LIKE '%%作弊%%') "
        "ORDER BY punch_time DESC LIMIT 10").arg(m_loginName);
    QSqlQuery rq(sqlAppeals);
    while (rq.next()) {
        QString pTime = rq.value(0).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
        QString status = rq.value(1).toString();
        recordCombo->addItem(pTime + " [" + status + "]", pTime + "|" + status);
    }

    if (recordCombo->count() == 0) {
        QMessageBox::information(parentWidget, "提示", "您最近没有异常考勤记录，无需申诉！"); return;
    }

    // 核心修复：明文过滤超级管理员
    QString sqlUsers = "SELECT name FROM users WHERE role IN ('管理员', '超级管理员') AND account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'";
    QSqlQuery q(sqlUsers);
    while (q.next()) {
        if (q.value(0).toString() != m_loginName) {
            approverCombo->addItem("👨‍💼 " + q.value(0).toString(), q.value(0).toString());
        }
    }

    form->addRow("选择异常记录: *", recordCombo);
    form->addRow("申诉事由: *", reasonEdit);
    form->addRow("审批人: *", approverCombo);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &formDlg);
    form->addRow(&buttonBox);
    connect(&buttonBox, &QDialogButtonBox::accepted, &formDlg, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &formDlg, &QDialog::reject);

    if (formDlg.exec() == QDialog::Accepted) {
        QString approver = approverCombo->currentData().toString();
        QString recordData = recordCombo->currentData().toString();
        QString pTime = recordData.split("|").first();
        QString pStatus = recordData.split("|").last();

        QString insertSql = QString("INSERT INTO appeals (applicant, abnormal_time, original_status, reason, approver, status) "
            "VALUES ('%1', '%2', '%3', '%4', '%5', '待审批')")
            .arg(m_loginName).arg(pTime).arg(pStatus).arg(reasonEdit->toPlainText()).arg(approver);
        QSqlQuery insertQ;
        insertQ.exec(insertSql);

        emit requestSendChat(approver, QString("【申诉审批】\n异常时间: %1\n原状态: %2\n事由: %3\n请前往审批。").arg(pTime, pStatus, reasonEdit->toPlainText()));
        QMessageBox::information(parentWidget, "成功", "申诉已提交！");
    }
}

void PunchModule::onAppealApproveClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();
    QDialog* apprDlg = new QDialog(parentWidget);
    apprDlg->setWindowTitle("考勤申诉待办");
    apprDlg->resize(700, 400);
    QVBoxLayout* layout = new QVBoxLayout(apprDlg);

    QTableWidget* table = new QTableWidget(apprDlg);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({ "申请人", "异常时间", "原状态", "事由", "状态", "操作" });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    QString sqlSelect = QString("SELECT id, applicant, abnormal_time, original_status, reason FROM appeals WHERE approver='%1' AND status='待审批'").arg(m_loginName);
    QSqlQuery query(sqlSelect);
    while (query.next()) {
        int row = table->rowCount(); table->insertRow(row);
        int reqId = query.value(0).toInt();
        QString applicant = query.value(1).toString();
        QString aTime = query.value(2).toDateTime().toString("yyyy-MM-dd HH:mm:ss");

        table->setItem(row, 0, new QTableWidgetItem(applicant));
        table->setItem(row, 1, new QTableWidgetItem(aTime));
        table->setItem(row, 2, new QTableWidgetItem(query.value(3).toString()));
        table->setItem(row, 3, new QTableWidgetItem(query.value(4).toString()));
        table->setItem(row, 4, new QTableWidgetItem("🔴 待处理"));

        QPushButton* btnPass = new QPushButton("✅ 批准并补卡");
        btnPass->setStyleSheet("color: white; background-color: #67C23A;");
        table->setCellWidget(row, 5, btnPass);

        connect(btnPass, &QPushButton::clicked, [=]() {
            QString updateQ1 = QString("UPDATE appeals SET status='已批准' WHERE id=%1").arg(reqId);
            QString updateQ2 = QString("UPDATE attendance_records SET status='正常(补卡)' WHERE name='%1' AND punch_time='%2'").arg(applicant, aTime);
            QSqlQuery updateQ;
            updateQ.exec(updateQ1);
            updateQ.exec(updateQ2);

            emit requestSendChat(applicant, QString("✅ 您 %1 的考勤申诉已通过！异常记录已变为正常(补卡)。").arg(aTime));
            QMessageBox::information(apprDlg, "成功", "已批准申诉，系统已自动为该员工补卡！");
            apprDlg->accept();
            });
    }
    layout->addWidget(table);
    apprDlg->exec();
    apprDlg->deleteLater();
}

void PunchModule::onLeaveRequestClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();
    QDialog typeDlg(parentWidget);
    typeDlg.setWindowTitle("请选择请假类型");
    typeDlg.resize(400, 500);
    QVBoxLayout* typeLayout = new QVBoxLayout(&typeDlg);
    QListWidget* typeList = new QListWidget(&typeDlg);
    typeList->setStyleSheet("QListWidget::item { height: 50px; border-bottom: 1px solid #EEE; font-size: 16px; }");
    QStringList types = { "事假 (按小时)", "调休 (按小时)", "病假 (按小时)", "年假 (按半天)", "产假 (按天)" };
    typeList->addItems(types);
    typeLayout->addWidget(typeList);
    QString selectedType = "";
    connect(typeList, &QListWidget::itemDoubleClicked, [&](QListWidgetItem* item) { selectedType = item->text().split(" ").first(); typeDlg.accept(); });
    if (typeDlg.exec() != QDialog::Accepted || selectedType.isEmpty()) return;

    QDialog formDlg(parentWidget);
    formDlg.setWindowTitle("发起请假提交 - " + selectedType); formDlg.resize(450, 600);
    QFormLayout* form = new QFormLayout(&formDlg);
    QDateTimeEdit* startEdit = new QDateTimeEdit(QDateTime::currentDateTime(), &formDlg);
    QDateTimeEdit* endEdit = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1), &formDlg);
    QLineEdit* durationEdit = new QLineEdit(&formDlg);
    QTextEdit* reasonEdit = new QTextEdit(&formDlg);
    QComboBox* approverCombo = new QComboBox(&formDlg);
    QComboBox* ccCombo = new QComboBox(&formDlg);

    // 核心修复：明文过滤超级管理员
    QString sqlUsers = "SELECT name, role FROM users WHERE account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'";
    QSqlQuery query(sqlUsers);
    while (query.next()) {
        QString n = query.value(0).toString();
        QString r = query.value(1).toString();

        if (n == m_loginName) continue;
        if (r == "管理员") {
            approverCombo->addItem("👨‍💼 " + n, n);
        }
        ccCombo->addItem("👤 " + n, n);
    }

    form->addRow("开始时间:", startEdit);
    form->addRow("结束时间:", endEdit);
    form->addRow("时长:", durationEdit);
    form->addRow("请假事由:", reasonEdit);
    form->addRow("审批人:", approverCombo);
    form->addRow("抄送人:", ccCombo);
    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &formDlg);
    form->addRow(&buttonBox);
    connect(&buttonBox, &QDialogButtonBox::accepted, &formDlg, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &formDlg, &QDialog::reject);

    if (formDlg.exec() == QDialog::Accepted) {
        QString approver = approverCombo->currentData().toString();
        QString cc = ccCombo->currentData().toString();

        QString insertSql = QString("INSERT INTO leave_requests (applicant, leave_type, start_time, end_time, duration, reason, approver, cc, status) "
            "VALUES ('%1', '%2', '%3', '%4', '%5', '%6', '%7', '%8', '待审批')")
            .arg(m_loginName).arg(selectedType).arg(startEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss"))
            .arg(endEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss")).arg(durationEdit->text())
            .arg(reasonEdit->toPlainText()).arg(approver).arg(cc);
        QSqlQuery insertQuery;
        insertQuery.exec(insertSql);

        emit requestSendChat(approver, QString("【请假审批】\n类型: %1\n事由: %2\n请审批。").arg(selectedType, reasonEdit->toPlainText()));
        QMessageBox::information(parentWidget, "成功", "申请已提交！");
    }
}

void PunchModule::onLeaveApproveClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();
    QDialog* apprDlg = new QDialog(parentWidget);
    apprDlg->setWindowTitle("我的审批待办");
    apprDlg->resize(800, 400);
    QVBoxLayout* layout = new QVBoxLayout(apprDlg);
    QTableWidget* table = new QTableWidget(apprDlg);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({ "申请人", "类型", "开始时间", "事由", "状态", "操作" });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    QString sqlSelect = QString("SELECT id, applicant, leave_type, start_time, reason FROM leave_requests WHERE approver='%1' AND status='待审批'").arg(m_loginName);
    QSqlQuery query(sqlSelect);
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

        connect(btnPass, &QPushButton::clicked, [=]() {
            QString updateQ1 = QString("UPDATE leave_requests SET status='已批准' WHERE id=%1").arg(reqId);
            QString updateQ2 = QString("INSERT INTO attendance_records (name, punch_time, status) VALUES ('%1', '%2', '📝 %3')").arg(applicant, sTime, lType);
            QSqlQuery updateQ;
            updateQ.exec(updateQ1);
            updateQ.exec(updateQ2);

            emit requestSendChat(applicant, QString("✅ 您的【%1】申请已通过！").arg(lType));
            QMessageBox::information(apprDlg, "成功", "已批准！");
            apprDlg->accept();
            });
    }
    layout->addWidget(table);
    apprDlg->exec();
    apprDlg->deleteLater();
}
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
#include <QDateTime>
#include <QTimeEdit>
#include <QSpinBox>
#include <QListWidget>
#include <QDateTimeEdit>
#include <QTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QProcess>
#include <QCheckBox>
#include <QTimer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>

// 同步请求函数
static QJsonObject requestDataFromServer(const QJsonObject& jsonRequest) {
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", 9999);
    QJsonObject responseJson;
    if (socket.waitForConnected(2000)) {
        QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
        socket.write(block);
        socket.waitForBytesWritten(1000);
        if (socket.waitForReadyRead(5000)) {
            QByteArray responseData;
            while (socket.waitForReadyRead(50) || socket.bytesAvailable() > 0) {
                responseData += socket.readAll();
                if (responseData.endsWith("\n")) break;
            }
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (!doc.isNull()) responseJson = doc.object();
        }
        socket.disconnectFromHost();
    }
    return responseJson;
}

static void sendCommandToServer(const QJsonObject& json) {
    requestDataFromServer(json);
}

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
    QJsonObject uReq;
    uReq["type"] = "query_user_dept";
    uReq["name"] = m_loginName;
    QJsonObject uRes = requestDataFromServer(uReq);
    QString myDept = uRes["department"].toString();

    if (myDept != "人力资源部") {
        if (m_btnRuleSettings) m_btnRuleSettings->setHidden(true);
    }
    else {
        if (m_btnRuleSettings) connect(m_btnRuleSettings, &QPushButton::clicked, this, &PunchModule::onRuleSettingsClicked);
    }

    if (m_role != "管理员登录" && m_role != "超级管理员" && m_role != "经理") {
        if (m_btnLeaveApprove) m_btnLeaveApprove->setHidden(true);
        if (m_btnAppealApprove) m_btnAppealApprove->setHidden(true);
    }

    loadRules(myDept);
    loadTodayPunchStatus();

    if (m_manualBtn) connect(m_manualBtn, &QPushButton::clicked, this, &PunchModule::onManualPunchClicked);
    if (m_btnLeaveRequest) connect(m_btnLeaveRequest, &QPushButton::clicked, this, &PunchModule::onLeaveRequestClicked);
    if (m_btnLeaveApprove) connect(m_btnLeaveApprove, &QPushButton::clicked, this, &PunchModule::onLeaveApproveClicked);
    if (m_btnAppealRequest) connect(m_btnAppealRequest, &QPushButton::clicked, this, &PunchModule::onAppealRequestClicked);
    if (m_btnAppealApprove) connect(m_btnAppealApprove, &QPushButton::clicked, this, &PunchModule::onAppealApproveClicked);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PunchModule::onTimeUpdate);
    m_timer->start(1000);
    onTimeUpdate();
}

void PunchModule::speakText(const QString& text) {
    QString command = QString("Add-Type -AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer).Speak('%1');").arg(text);
    QProcess::startDetached("powershell", QStringList() << "-WindowStyle" << "Hidden" << "-Command" << command);
}

void PunchModule::initRulesTable() {}

void PunchModule::loadRules(QString myDept) {
    QJsonObject req;
    req["type"] = "query_shift_rule";
    req["dept"] = myDept;
    QJsonObject res = requestDataFromServer(req);
    if (res["status"].toString() == "success") {
        m_shiftName = res["rule_name"].toString();
        m_startTime = QTime::fromString(res["start_time"].toString(), "HH:mm:ss");
        m_endTime = QTime::fromString(res["end_time"].toString(), "HH:mm:ss");
        m_lateMins = res["late_mins"].toInt();
        m_absentMins = res["absent_mins"].toInt();
        if (m_lblMorningTime) m_lblMorningTime->setText(QString("<div style='text-align:center;'><span style='font-size:12px; color:#888;'>[%1]</span><br><span style='font-size:16px; font-weight:bold;'>上班 %2</span></div>").arg(m_shiftName, m_startTime.toString("HH:mm")));
        if (m_lblEveningTime) m_lblEveningTime->setText(QString("<div style='text-align:center;'><span style='font-size:12px; color:#888;'>[%1]</span><br><span style='font-size:16px; font-weight:bold;'>下班 %2</span></div>").arg(m_shiftName, m_endTime.toString("HH:mm")));
    }
}

void PunchModule::loadTodayPunchStatus() {
    if (m_loginName.isEmpty()) return;
    QJsonObject req;
    req["type"] = "query_today_status";
    req["name"] = m_loginName;
    QJsonObject res = requestDataFromServer(req);
    if (res["status"].toString() != "success") return;
    bool isOnLeave = res["is_on_leave"].toBool();
    QJsonArray punches = res["punches"].toArray();
    bool hasMorning = false;
    for (int i = 0; i < punches.size(); ++i) {
        QJsonObject p = punches[i].toObject();
        QTime pTime = QTime::fromString(p["time"].toString(), "HH:mm:ss");
        QString statusStr = p["status"].toString();
        QString displayTxt = QString("<div style='text-align:center;'><span style='font-size:16px; font-weight:bold;'>%1</span><br><span style='font-size:12px;'>%2</span></div>").arg(pTime.toString("HH:mm"), statusStr);
        if (pTime < QTime(12, 0) && !hasMorning) {
            m_lblMorningStatus->setText(displayTxt);
            m_lblMorningStatus->setStyleSheet(statusStr.contains("正常") ? "color: #67C23A;" : "color: #F56C6C;");
            hasMorning = true;
        }
        else if (pTime >= QTime(12, 0)) {
            m_lblEveningStatus->setText(displayTxt);
            m_lblEveningStatus->setStyleSheet(statusStr.contains("正常") ? "color: #409EFF;" : "color: #E6A23C;");
        }
    }
    if (punches.isEmpty() && !isOnLeave) {
        m_lblMorningStatus->setText("<div style='text-align:center;'><span style='font-size:16px; font-weight:bold;'>缺卡</span><br><span style='font-size:12px;'>旷工</span></div>");
        m_lblMorningStatus->setStyleSheet("color: #F56C6C;");
    }
    else if (punches.isEmpty() && isOnLeave) {
        m_lblMorningStatus->setText("<div style='text-align:center;'><span style='font-size:16px; font-weight:bold;'>请假</span><br><span style='font-size:12px;'>已准假</span></div>");
        m_lblMorningStatus->setStyleSheet("color: #E6A23C;");
    }
}

QString PunchModule::calculatePunchStatus(const QTime& punchTime) {
    if (punchTime < QTime(12, 0)) {
        int secsLate = m_startTime.secsTo(punchTime);
        if (secsLate <= 0) return "正常打卡";
        return (secsLate > m_absentMins * 60) ? "旷工" : "迟到";
    }
    return (punchTime < m_endTime) ? "早退" : "正常下班";
}

// 🚀 核心改造：排班规则设置 UI 美化
void PunchModule::onRuleSettingsClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("⚙️ 排班规则设置");
    dialog.resize(400, 360); // 适度撑大窗口，避免拥挤

    QFormLayout* form = new QFormLayout(&dialog);
    form->setContentsMargins(30, 25, 30, 25); // 增加四周内边距
    form->setSpacing(18);                     // 增加行间距

    // 🌟 全局高颜值输入框 CSS 样式
    QString modernInputStyle =
        "QComboBox, QTimeEdit, QSpinBox { border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 10px; background: white; color: #606266; font-size: 13px; min-height: 28px; }"
        "QComboBox:hover, QTimeEdit:hover, QSpinBox:hover { border-color: #C0C4CC; }"
        "QComboBox:focus, QTimeEdit:focus, QSpinBox:focus { border-color: #409EFF; }"
        "QComboBox::drop-down, QTimeEdit::drop-down, QSpinBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 25px; border-left: none; }"
        "QComboBox::down-arrow, QTimeEdit::down-arrow, QSpinBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid #909399; }";

    QComboBox* shiftCombo = new QComboBox(&dialog);
    shiftCombo->addItems({ "常规班", "早班", "晚班" });
    shiftCombo->setStyleSheet(modernInputStyle);

    QComboBox* deptCombo = new QComboBox(&dialog);
    deptCombo->addItems({ "全部", "总经办", "人力资源部", "财务部", "研发部", "市场部", "销售部", "客户服务部" });
    deptCombo->setStyleSheet(modernInputStyle);

    QTimeEdit* startEdit = new QTimeEdit(QTime(9, 0), &dialog);
    startEdit->setDisplayFormat("HH:mm");
    startEdit->setStyleSheet(modernInputStyle);

    QTimeEdit* endEdit = new QTimeEdit(QTime(18, 0), &dialog);
    endEdit->setDisplayFormat("HH:mm");
    endEdit->setStyleSheet(modernInputStyle);

    QSpinBox* lateEdit = new QSpinBox(&dialog);
    lateEdit->setSuffix(" 分钟");
    lateEdit->setValue(30);
    lateEdit->setMaximum(300);
    lateEdit->setStyleSheet(modernInputStyle);

    QSpinBox* absentEdit = new QSpinBox(&dialog);
    absentEdit->setSuffix(" 分钟");
    absentEdit->setValue(120);
    absentEdit->setMaximum(600);
    absentEdit->setStyleSheet(modernInputStyle);

    form->addRow("排班名称:", shiftCombo);
    form->addRow("适用部门:", deptCombo);
    form->addRow("上班时间:", startEdit);
    form->addRow("下班时间:", endEdit);
    form->addRow("迟到判定:", lateEdit);
    form->addRow("旷工判定:", absentEdit);

    // ── 按钮 (✨ UI 升级) ──
    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    bb->button(QDialogButtonBox::Save)->setText("保存设置");
    bb->button(QDialogButtonBox::Cancel)->setText("取消");

    // 给保存按钮加上主题蓝，取消按钮加上悬浮效果
    bb->button(QDialogButtonBox::Save)->setStyleSheet("QPushButton { background-color: #409EFF; color: white; border-radius: 4px; padding: 6px 20px; font-weight: bold; border: none; } QPushButton:hover { background-color: #66B1FF; }");
    bb->button(QDialogButtonBox::Cancel)->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #606266; border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 20px; } QPushButton:hover { color: #409EFF; border-color: #c6e2ff; background-color: #ecf5ff; }");

    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // 绝对不改服务端逻辑
    if (dialog.exec() == QDialog::Accepted) {
        QJsonObject req; req["type"] = "rule_settings";
        req["dept"] = deptCombo->currentText(); req["rule_name"] = shiftCombo->currentText();
        req["start_time"] = startEdit->time().toString("HH:mm:ss"); req["end_time"] = endEdit->time().toString("HH:mm:ss");
        req["late_mins"] = lateEdit->value(); req["absent_mins"] = absentEdit->value();
        sendCommandToServer(req);
        QMessageBox::information(nullptr, "成功", "排班规则已提交！");
        QTimer::singleShot(500, this, [=]() { loadRules(deptCombo->currentText()); loadTodayPunchStatus(); });
    }
}

void PunchModule::onManualPunchClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("手动打卡辅助");
    QFormLayout* form = new QFormLayout(&dialog);
    QLineEdit* nameEdit = new QLineEdit(&dialog);
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form->addRow("姓名:", nameEdit); form->addRow(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        QString claimName = nameEdit->text().trimmed();
        if (claimName != m_loginName) { QMessageBox::warning(nullptr, "拒绝", "只能为本人打卡！"); return; }
        if (claimName == m_currentFaceName && !m_currentFaceName.isEmpty() && m_currentFaceName != "未知访客") {
            QJsonObject req; req["type"] = "punch_request"; req["name"] = claimName;
            sendCommandToServer(req);
            speakText("考勤指令已下发");
            QTimer::singleShot(500, this, &PunchModule::loadTodayPunchStatus);
        }
        else {
            m_cheatCount++;
            if (m_cheatCount >= 3) {
                QJsonObject req; req["type"] = "punch_cheat"; req["name"] = claimName;
                sendCommandToServer(req);
                m_cheatCount = 0;
            }
            else { QMessageBox::warning(nullptr, "失败", "人脸核验不匹配！"); }
        }
    }
}

void PunchModule::onAppealRequestClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();

    QJsonObject initReq;
    initReq["type"] = "query_approval_candidates";
    initReq["name"] = m_loginName;
    QJsonObject initRes = requestDataFromServer(initReq);

    if (initRes.isEmpty() || !initRes.contains("my_role")) {
        QMessageBox::critical(parentWidget, "错误", "无法连接服务器获取审批信息，请检查网络！");
        return;
    }

    QString applicantRole = initRes["my_role"].toString();
    QString applicantDept = initRes["my_dept"].toString();
    QString applicantJob = initRes["my_job"].toString();

    QJsonArray abnArr = initRes["abnormal_records"].toArray();
    QJsonArray hrArr = initRes["hr_list"].toArray();
    QJsonArray gmArr = initRes["gm_list"].toArray();
    QJsonArray mgrArr = initRes["mgr_list"].toArray();

    if (abnArr.isEmpty()) {
        QMessageBox::information(parentWidget, "无异常", "您最近10天内没有异常考勤记录，无需申诉！");
        return;
    }

    QDialog formDlg(parentWidget);
    formDlg.setWindowTitle("发起考勤异常申诉");
    formDlg.resize(500, 450);
    QFormLayout* form = new QFormLayout(&formDlg);
    form->setContentsMargins(25, 25, 25, 25);
    form->setSpacing(15);

    QString modernInputStyle =
        "QComboBox, QTextEdit { border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 10px; background: white; color: #606266; font-size: 13px; min-height: 28px; }"
        "QComboBox:hover, QTextEdit:hover { border-color: #C0C4CC; }"
        "QComboBox:focus, QTextEdit:focus { border-color: #409EFF; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 25px; border-left: none; }"
        "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid #909399; }";

    QComboBox* recordCombo = new QComboBox(&formDlg);
    recordCombo->setStyleSheet(modernInputStyle);
    for (int i = 0; i < abnArr.size(); ++i) {
        QJsonObject rec = abnArr[i].toObject();
        recordCombo->addItem(rec["display"].toString(), rec["time"].toString());
    }

    QTextEdit* reasonEdit = new QTextEdit(&formDlg);
    reasonEdit->setPlaceholderText("请输入您申诉的详细理由（如：因见客户未能及时打卡...）");
    reasonEdit->setMaximumHeight(80);
    reasonEdit->setStyleSheet(modernInputStyle);

    form->addRow("异常记录:", recordCombo);
    form->addRow("申诉理由:", reasonEdit);

    QComboBox* app1 = new QComboBox(&formDlg);
    QComboBox* app2 = new QComboBox(&formDlg);
    QComboBox* app3 = new QComboBox(&formDlg);

    app1->setStyleSheet(modernInputStyle);
    app2->setStyleSheet(modernInputStyle);
    app3->setStyleSheet(modernInputStyle);

    auto fillFromArray = [](QComboBox* cb, const QJsonArray& arr, const QString& prefix) {
        cb->clear();
        for (int i = 0; i < arr.size(); ++i) {
            QString name = arr[i].toString();
            cb->addItem(prefix + name, name);
        }
        };
    auto fillHR = [&](QComboBox* cb) { fillFromArray(cb, hrArr, "🏢 人资经理: ");  };
    auto fillGM = [&](QComboBox* cb) { fillFromArray(cb, gmArr, "👑 总经理: ");    };
    auto fillDeptMgr = [&](QComboBox* cb) { fillFromArray(cb, mgrArr, "👨‍💼 部门经理: "); };

    bool isGM = (applicantDept == "总经办" && (applicantJob == "总经理" || applicantJob == "总裁"));
    bool isHRManager = (applicantDept == "人力资源部" && applicantJob == "部门经理");
    bool isTwoLevel = false;

    if (!isGM && !isHRManager) {
        if (applicantDept == "人力资源部") isTwoLevel = true;
        else if (applicantJob == "部门经理") isTwoLevel = true;
        else if (applicantDept == "总经办") isTwoLevel = true;
    }

    int approvalLevels = 0;

    if (isGM) {
        approvalLevels = 1;
        fillHR(app1);
        form->addRow("第一审批人(人资经理):", app1);
        app2->setVisible(false); app3->setVisible(false);
    }
    else if (isHRManager) {
        approvalLevels = 1;
        fillGM(app1);
        form->addRow("第一审批人(总经理):", app1);
        app2->setVisible(false); app3->setVisible(false);
    }
    else if (isTwoLevel) {
        approvalLevels = 2;
        fillHR(app1); fillGM(app2);
        form->addRow("第一审批人(人资经理):", app1);
        form->addRow("第二审批人(总经理):", app2);
        app3->setVisible(false);
    }
    else {
        approvalLevels = 3;
        fillDeptMgr(app1); fillHR(app2); fillGM(app3);
        form->addRow("第一审批人(部门经理):", app1);
        form->addRow("第二审批人(人资经理):", app2);
        form->addRow("第三审批人(总经理):", app3);
    }

    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &formDlg);
    bb->button(QDialogButtonBox::Ok)->setText("提交申诉");
    bb->button(QDialogButtonBox::Ok)->setStyleSheet("QPushButton { background-color: #409EFF; color: white; border-radius: 4px; padding: 6px 20px; font-weight: bold; border: none; } QPushButton:hover { background-color: #66B1FF; }");
    bb->button(QDialogButtonBox::Cancel)->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #606266; border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 20px; } QPushButton:hover { color: #409EFF; border-color: #c6e2ff; background-color: #ecf5ff; }");

    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &formDlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &formDlg, &QDialog::reject);

    if (formDlg.exec() == QDialog::Accepted) {
        if (app1->count() == 0) {
            QMessageBox::critical(parentWidget, "失败", "无有效的第一审批人，请联系管理员！");
            return;
        }

        QString reasonText = reasonEdit->toPlainText().trimmed();
        if (reasonText.isEmpty()) {
            QMessageBox::warning(parentWidget, "提示", "必须填写申诉理由！");
            return;
        }

        QJsonArray approverChain;
        approverChain.append(app1->currentData().toString());
        if (approvalLevels >= 2 && app2->count() > 0) approverChain.append(app2->currentData().toString());
        if (approvalLevels >= 3 && app3->count() > 0) approverChain.append(app3->currentData().toString());

        QStringList chainList;
        for (int i = 0; i < approverChain.size(); ++i) {
            chainList << approverChain[i].toString();
        }

        QJsonObject req;
        req["type"] = "appeal_request";
        req["applicant"] = m_loginName;
        req["abnormal_time"] = recordCombo->currentData().toString();

        QString displayStr = recordCombo->currentText();
        int bracketIndex = displayStr.indexOf('[');
        if (bracketIndex != -1) {
            req["original_status"] = displayStr.mid(bracketIndex + 1).remove(']');
        }
        else {
            req["original_status"] = "异常";
        }

        req["reason"] = reasonText;
        req["approver"] = chainList.join(",");

        sendCommandToServer(req);

        QMessageBox::information(parentWidget, "成功",
            QString("异常申诉已提交，当前进入 %1 级审批流程。\n审批链：%2")
            .arg(approvalLevels)
            .arg(chainList.join(" → ")));

        QString notifyMsg = QString("%1 提交了考勤异常申诉，请及时审批").arg(m_loginName);
        emit requestSendChat(notifyMsg);
    }
}

void PunchModule::onLeaveRequestClicked()
{
    QWidget* parentWidget = qobject_cast<QWidget*>(this->parent());

    QJsonObject initReq;
    initReq["type"] = "query_approval_candidates";
    initReq["name"] = m_loginName;
    QJsonObject initRes = requestDataFromServer(initReq);

    if (initRes.isEmpty() || !initRes.contains("my_role")) {
        QMessageBox::critical(parentWidget, "错误", "无法连接服务器获取审批信息，请检查网络连接和服务器状态！");
        return;
    }

    QString applicantRole = initRes["my_role"].toString();
    QString applicantDept = initRes["my_dept"].toString();
    QString applicantJob = initRes["my_job"].toString();

    QJsonArray hrArr = initRes["hr_list"].toArray();
    QJsonArray gmArr = initRes["gm_list"].toArray();
    QJsonArray mgrArr = initRes["mgr_list"].toArray();

    QDialog typeDlg(parentWidget);
    typeDlg.setWindowTitle("请选择请假类型");
    typeDlg.resize(300, 350);
    QVBoxLayout* typeLayout = new QVBoxLayout(&typeDlg);
    QListWidget* typeList = new QListWidget(&typeDlg);

    typeList->setStyleSheet(
        "QListWidget { font-size: 15px; border: 1px solid #E4E7ED; border-radius: 8px; outline: none; background: #FFFFFF; }"
        "QListWidget::item { padding: 15px; border-bottom: 1px solid #EBEEF5; color: #303133; }"
        "QListWidget::item:hover { background-color: #F5F7FA; border-radius: 4px; }"
        "QListWidget::item:selected { background-color: #ECF5FF; color: #409EFF; font-weight: bold; border-radius: 4px; }"
    );
    typeList->addItems({ "事假", "调休", "病假", "年假", "产假" });
    typeLayout->addWidget(typeList);

    QString selectedType;
    connect(typeList, &QListWidget::itemDoubleClicked, [&](QListWidgetItem* item) {
        selectedType = item->text();
        typeDlg.accept();
        });
    if (typeDlg.exec() != QDialog::Accepted || selectedType.isEmpty()) return;

    QDialog formDlg(parentWidget);
    formDlg.setWindowTitle("发起请假申请 - " + selectedType);
    formDlg.resize(500, 450);
    QFormLayout* form = new QFormLayout(&formDlg);
    form->setContentsMargins(25, 25, 25, 25);
    form->setSpacing(15);

    QString modernInputStyle =
        "QDateTimeEdit, QComboBox, QTextEdit { border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 10px; background: white; color: #606266; font-size: 13px; min-height: 28px; }"
        "QDateTimeEdit:hover, QComboBox:hover, QTextEdit:hover { border-color: #C0C4CC; }"
        "QDateTimeEdit:focus, QComboBox:focus, QTextEdit:focus { border-color: #409EFF; }"
        "QDateTimeEdit::drop-down, QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 25px; border-left: none; }"
        "QDateTimeEdit::down-arrow, QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid #909399; }";

    QDateTimeEdit* startEdit = new QDateTimeEdit(QDateTime::currentDateTime(), &formDlg);
    QDateTimeEdit* endEdit = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1), &formDlg);

    startEdit->setCalendarPopup(true);
    endEdit->setCalendarPopup(true);
    startEdit->setDisplayFormat("yyyy-MM-dd HH:mm");
    endEdit->setDisplayFormat("yyyy-MM-dd HH:mm");
    startEdit->setStyleSheet(modernInputStyle);
    endEdit->setStyleSheet(modernInputStyle);

    QTextEdit* reasonEdit = new QTextEdit(&formDlg);
    reasonEdit->setPlaceholderText("请输入请假理由...");
    reasonEdit->setMaximumHeight(80);
    reasonEdit->setStyleSheet(modernInputStyle);

    form->addRow("开始时间:", startEdit);
    form->addRow("结束时间:", endEdit);
    form->addRow("请假理由:", reasonEdit);

    QComboBox* app1 = new QComboBox(&formDlg);
    QComboBox* app2 = new QComboBox(&formDlg);
    QComboBox* app3 = new QComboBox(&formDlg);

    app1->setStyleSheet(modernInputStyle);
    app2->setStyleSheet(modernInputStyle);
    app3->setStyleSheet(modernInputStyle);

    auto fillFromArray = [](QComboBox* cb, const QJsonArray& arr, const QString& prefix) {
        cb->clear();
        for (int i = 0; i < arr.size(); ++i) {
            QString name = arr[i].toString();
            cb->addItem(prefix + name, name);
        }
        };
    auto fillHR = [&](QComboBox* cb) { fillFromArray(cb, hrArr, "🏢 人资经理: ");  };
    auto fillGM = [&](QComboBox* cb) { fillFromArray(cb, gmArr, "👑 总经理: ");    };
    auto fillDeptMgr = [&](QComboBox* cb) { fillFromArray(cb, mgrArr, "👨‍💼 部门经理: "); };

    bool isGM = (applicantDept == "总经办" && (applicantJob == "总经理" || applicantJob == "总裁"));
    bool isHRManager = (applicantDept == "人力资源部" && applicantJob == "部门经理");
    bool isTwoLevel = false;

    if (!isGM && !isHRManager) {
        if (applicantDept == "人力资源部") isTwoLevel = true;
        else if (applicantJob == "部门经理") isTwoLevel = true;
        else if (applicantDept == "总经办") isTwoLevel = true;
    }

    int approvalLevels = 0;

    if (isGM) {
        approvalLevels = 1;
        fillHR(app1);
        form->addRow("第一审批人(人资经理):", app1);
        app2->setVisible(false); app3->setVisible(false);
    }
    else if (isHRManager) {
        approvalLevels = 1;
        fillGM(app1);
        form->addRow("第一审批人(总经理):", app1);
        app2->setVisible(false); app3->setVisible(false);
    }
    else if (isTwoLevel) {
        approvalLevels = 2;
        fillHR(app1); fillGM(app2);
        form->addRow("第一审批人(人资经理):", app1);
        form->addRow("第二审批人(总经理):", app2);
        app3->setVisible(false);
    }
    else {
        approvalLevels = 3;
        fillDeptMgr(app1); fillHR(app2); fillGM(app3);
        form->addRow("第一审批人(部门经理):", app1);
        form->addRow("第二审批人(人资经理):", app2);
        form->addRow("第三审批人(总经理):", app3);
    }

    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &formDlg);
    bb->button(QDialogButtonBox::Ok)->setText("提交申请");
    bb->button(QDialogButtonBox::Ok)->setStyleSheet("QPushButton { background-color: #409EFF; color: white; border-radius: 4px; padding: 6px 20px; font-weight: bold; border: none; } QPushButton:hover { background-color: #66B1FF; }");
    bb->button(QDialogButtonBox::Cancel)->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #606266; border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 20px; } QPushButton:hover { color: #409EFF; border-color: #c6e2ff; background-color: #ecf5ff; }");

    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &formDlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &formDlg, &QDialog::reject);

    if (formDlg.exec() == QDialog::Accepted) {
        if (app1->count() == 0) {
            QMessageBox::critical(parentWidget, "失败", "无有效的第一审批人，请联系管理员！");
            return;
        }

        if (startEdit->dateTime() >= endEdit->dateTime()) {
            QMessageBox::warning(parentWidget, "时间填写错误", "结束时间必须晚于开始时间，请重新选择！");
            return;
        }

        QJsonArray approverChain;
        approverChain.append(app1->currentData().toString());
        if (approvalLevels >= 2 && app2->count() > 0) approverChain.append(app2->currentData().toString());
        if (approvalLevels >= 3 && app3->count() > 0) approverChain.append(app3->currentData().toString());

        QStringList chainList;
        for (int i = 0; i < approverChain.size(); ++i) {
            chainList << approverChain[i].toString();
        }

        QJsonObject req;
        req["type"] = "leave_request";
        req["applicant"] = m_loginName;
        req["leave_type"] = selectedType;
        req["start_time"] = startEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss");
        req["end_time"] = endEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss");
        req["reason"] = reasonEdit->toPlainText().trimmed();
        req["approver"] = chainList.join(",");
        req["approval_levels"] = approvalLevels;
        req["approver_chain"] = approverChain;

        sendCommandToServer(req);

        QMessageBox::information(parentWidget, "成功",
            QString("请假申请已提交，当前进入 %1 级审批流程。\n审批链：%2")
            .arg(approvalLevels)
            .arg(chainList.join(" → ")));

        QString notifyMsg = QString("%1 提交了 [%2] 请假申请，请及时审批")
            .arg(m_loginName, selectedType);
        emit requestSendChat(notifyMsg);
    }
}

void PunchModule::onLeaveApproveClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();
    QDialog apprDlg(parentWidget); apprDlg.setWindowTitle("假条审批"); apprDlg.resize(600, 400);
    QVBoxLayout* layout = new QVBoxLayout(&apprDlg);
    QTableWidget* table = new QTableWidget(&apprDlg);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({ "申请人", "类型", "开始时间", "理由", "状态", "操作" });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    QJsonObject req; req["type"] = "query_pending_leaves"; req["approver"] = m_loginName;
    QJsonObject res = requestDataFromServer(req);
    if (res["status"].toString() == "success") {
        QJsonArray arr = res["data"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject rowObj = arr[i].toObject();
            int row = table->rowCount(); table->insertRow(row);
            int reqId = rowObj["id"].toInt(); QString applicant = rowObj["applicant"].toString();
            QString sTime = rowObj["start"].toString(); QString eTime = rowObj["end"].toString();
            QString lType = rowObj["type"].toString();

            table->setItem(row, 0, new QTableWidgetItem(applicant));
            table->setItem(row, 1, new QTableWidgetItem(lType));
            table->setItem(row, 2, new QTableWidgetItem(sTime));
            table->setItem(row, 3, new QTableWidgetItem(rowObj["reason"].toString()));
            table->setItem(row, 4, new QTableWidgetItem("待审批"));
            QPushButton* btnPass = new QPushButton("批准");
            btnPass->setStyleSheet("background-color: #67C23A; color: white; border-radius:3px; padding:4px;");
            table->setCellWidget(row, 5, btnPass);

            connect(btnPass, &QPushButton::clicked, [=, &apprDlg]() {
                QJsonObject passReq; passReq["type"] = "leave_approve";
                passReq["reqId"] = reqId; passReq["applicant"] = applicant;
                passReq["start_time"] = sTime; passReq["end_time"] = eTime; passReq["leave_type"] = lType;
                sendCommandToServer(passReq);
                apprDlg.accept();
                });
        }
    }
    layout->addWidget(table); apprDlg.exec();
}

void PunchModule::onAppealApproveClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();
    QDialog apprDlg(parentWidget);
    apprDlg.setWindowTitle("申诉审批中心");
    apprDlg.resize(750, 400);
    QVBoxLayout* layout = new QVBoxLayout(&apprDlg);
    QTableWidget* table = new QTableWidget(&apprDlg);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({ "申请人", "异常时间/日期", "申诉类型", "理由", "状态", "操作" });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    QJsonObject req;
    req["type"] = "query_pending_appeals";
    req["approver"] = m_loginName;
    QJsonObject res = requestDataFromServer(req);

    if (res["status"].toString() == "success") {
        QJsonArray arr = res["data"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject rowObj = arr[i].toObject();
            int row = table->rowCount(); table->insertRow(row);
            int reqId = rowObj["id"].toInt();
            QString applicant = rowObj["applicant"].toString();
            QString aTime = rowObj["time"].toString();
            QString aType = rowObj["type"].toString();

            table->setItem(row, 0, new QTableWidgetItem(applicant));
            table->setItem(row, 1, new QTableWidgetItem(aTime));
            table->setItem(row, 2, new QTableWidgetItem(aType));
            table->setItem(row, 3, new QTableWidgetItem(rowObj["reason"].toString()));
            table->setItem(row, 4, new QTableWidgetItem("待审批"));

            QPushButton* btnPass = new QPushButton("修正记录");
            btnPass->setStyleSheet("background-color: #409EFF; color: white; border-radius:3px; padding:4px;");
            table->setCellWidget(row, 5, btnPass);

            connect(btnPass, &QPushButton::clicked, [=, &apprDlg]() {
                QJsonObject passReq;
                passReq["type"] = "appeal_approve";
                passReq["reqId"] = reqId;
                passReq["applicant"] = applicant;
                passReq["abnormal_time"] = aTime;
                passReq["appeal_type"] = aType;
                sendCommandToServer(passReq);

                QMessageBox::information(&apprDlg, "已处理", "指令已下发，服务器将完美修正记录！");
                apprDlg.accept();
                });
        }
    }
    layout->addWidget(table);
    apprDlg.exec();
}

void PunchModule::onTimeUpdate() {
    if (m_lblCurrentTime) m_lblCurrentTime->setText("当前时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
}

void PunchModule::renderFrame(const QImage& img) {
    if (m_cameraLabel && !img.isNull()) m_cameraLabel->setPixmap(QPixmap::fromImage(img).scaled(m_cameraLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void PunchModule::updateRecognizedName(const QString& name) {
    m_currentFaceName = name;
}
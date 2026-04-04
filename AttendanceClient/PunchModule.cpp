#include "PunchModule.h"
#include "NetworkHelper.h" 
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
#include <QMap>
#include <QCheckBox>
#include <QTimer>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
// 构造函数，初始化打卡模块并按角色绑定界面
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
    m_lblCurrentTime(currentTimeLabel), m_role(role), m_loginName(loginName)
{
    // 查询当前用户部门
    QJsonObject uReq;
    uReq["type"] = "query_user_dept";
    uReq["name"] = m_loginName;
    QJsonObject uRes = NetworkHelper::request(uReq);
    QString myDept = uRes["department"].toString();
    // 人资部门才显示排班设置
    if (myDept != "人力资源部") {
        if (m_btnRuleSettings) m_btnRuleSettings->setHidden(true);
    }
    else {
        if (m_btnRuleSettings) connect(m_btnRuleSettings, &QPushButton::clicked, this, &PunchModule::onRuleSettingsClicked);
    }
    // 普通员工隐藏审批按钮
    if (m_role != "管理员登录" && m_role != "经理") {
        if (m_btnLeaveApprove) m_btnLeaveApprove->setHidden(true);
        if (m_btnAppealApprove) m_btnAppealApprove->setHidden(true);
    }
    loadRules(myDept);
    loadTodayPunchStatus();
    // 绑定按钮事件
    if (m_manualBtn) connect(m_manualBtn, &QPushButton::clicked, this, &PunchModule::onManualPunchClicked);
    if (m_btnLeaveRequest) connect(m_btnLeaveRequest, &QPushButton::clicked, this, &PunchModule::onLeaveRequestClicked);
    if (m_btnLeaveApprove) connect(m_btnLeaveApprove, &QPushButton::clicked, this, &PunchModule::onLeaveApproveClicked);
    if (m_btnAppealRequest) connect(m_btnAppealRequest, &QPushButton::clicked, this, &PunchModule::onAppealRequestClicked);
    if (m_btnAppealApprove) connect(m_btnAppealApprove, &QPushButton::clicked, this, &PunchModule::onAppealApproveClicked);
    // 启动时间刷新定时器
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PunchModule::onTimeUpdate);
    m_timer->start(1000);
    onTimeUpdate();
}
// 播放语音提示
void PunchModule::speakText(const QString& text) {
    QString command = QString("Add-Type -AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer).Speak('%1');").arg(text);
    QProcess::startDetached("powershell", QStringList() << "-WindowStyle" << "Hidden" << "-Command" << command);
}
// 预留排班表初始化入口
void PunchModule::initRulesTable() {}
// 加载当前部门的排班规则
void PunchModule::loadRules(QString myDept) {
    QJsonObject req;
    req["type"] = "query_shift_rule";
    req["dept"] = myDept;
    QJsonObject res = NetworkHelper::request(req);
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
// 加载当天考勤状态并刷新界面
void PunchModule::loadTodayPunchStatus() {
    if (m_loginName.isEmpty()) return;
    QJsonObject req;
    req["type"] = "query_today_status";
    req["name"] = m_loginName;
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() != "success") return;
    bool isOnLeave = res["is_on_leave"].toBool();
    // 请假期间直接显示请假状态
    if (isOnLeave) {
        QString leaveHtml = "<div style='text-align:center;'><span style='font-size:16px; font-weight:bold;'>请假</span><br><span style='font-size:12px;'>已准假</span></div>";
        if (m_lblMorningStatus) {
            m_lblMorningStatus->setText(leaveHtml);
            m_lblMorningStatus->setStyleSheet("color: #E6A23C;");
        }
        if (m_lblEveningStatus) {
            m_lblEveningStatus->setText(leaveHtml);
            m_lblEveningStatus->setStyleSheet("color: #E6A23C;");
        }
        return;
    }
    QJsonArray punches = res["punches"].toArray();
    bool hasMorning = false;
    bool hasEvening = false;
    // 遍历当天打卡记录
    for (int i = 0; i < punches.size(); ++i) {
        QJsonObject p = punches[i].toObject();
        QTime pTime = QTime::fromString(p["time"].toString(), "HH:mm:ss");
        QString statusStr = p["status"].toString();
        QString displayTxt = QString("<div style='text-align:center;'><span style='font-size:16px; font-weight:bold;'>%1</span><br><span style='font-size:12px;'>%2</span></div>").arg(pTime.toString("HH:mm"), statusStr);
        if (pTime < QTime(12, 0)) {
            // 记录最早的上午打卡
            if (!hasMorning) {
                m_lblMorningStatus->setText(displayTxt);
                m_lblMorningStatus->setStyleSheet(statusStr.contains("正常") || statusStr.contains("修正") ? "color: #67C23A;" : "color: #F56C6C;");
                hasMorning = true;
            }
        }
        else if (pTime >= QTime(12, 0)) {
            // 记录最后的下午打卡
            m_lblEveningStatus->setText(displayTxt);
            m_lblEveningStatus->setStyleSheet(statusStr.contains("正常") || statusStr.contains("修正") || statusStr.contains("下班") ? "color: #409EFF;" : "color: #E6A23C;");
            hasEvening = true;
        }
    }
    // 没有记录时显示缺卡
    if (!hasMorning) {
        m_lblMorningStatus->setText("<div style='text-align:center;'><span style='font-size:16px; font-weight:bold;'>缺卡</span><br><span style='font-size:12px;'>待打卡/旷工</span></div>");
        m_lblMorningStatus->setStyleSheet("color: #F56C6C;");
    }
    if (!hasEvening) {
        m_lblEveningStatus->setText("<div style='text-align:center;'><span style='font-size:16px; font-weight:bold;'>缺卡</span><br><span style='font-size:12px;'>待打卡</span></div>");
        m_lblEveningStatus->setStyleSheet("color: #F56C6C;");
    }
}
// [已废弃] 打卡状态判定已移至服务端执行，此方法保留空实现以兼容编译
QString PunchModule::calculatePunchStatus(const QTime& /*punchTime*/) {
    return "未知";
}
// 打开排班设置窗口
void PunchModule::onRuleSettingsClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("排班规则设置");
    dialog.resize(400, 360);
    QFormLayout* form = new QFormLayout(&dialog);
    form->setContentsMargins(30, 25, 30, 25);
    form->setSpacing(18);
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
    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    bb->button(QDialogButtonBox::Save)->setText("保存设置");
    bb->button(QDialogButtonBox::Cancel)->setText("取消");
    bb->button(QDialogButtonBox::Save)->setStyleSheet("QPushButton { background-color: #409EFF; color: white; border-radius: 4px; padding: 6px 20px; font-weight: bold; border: none; } QPushButton:hover { background-color: #66B1FF; }");
    bb->button(QDialogButtonBox::Cancel)->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #606266; border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 20px; } QPushButton:hover { color: #409EFF; border-color: #c6e2ff; background-color: #ecf5ff; }");
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        QJsonObject req;
        req["type"] = "rule_settings";
        req["dept"] = deptCombo->currentText();
        req["rule_name"] = shiftCombo->currentText();
        req["start_time"] = startEdit->time().toString("HH:mm:ss");
        req["end_time"] = endEdit->time().toString("HH:mm:ss");
        req["late_mins"] = lateEdit->value();
        req["absent_mins"] = absentEdit->value();
        NetworkHelper::request(req);
        QMessageBox::information(nullptr, "成功", "排班规则已提交！");
        QTimer::singleShot(500, this, [=]() { loadRules(deptCombo->currentText()); loadTodayPunchStatus(); });
    }
}
// 处理手动打卡
void PunchModule::onManualPunchClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("手动打卡辅助");
    QFormLayout* form = new QFormLayout(&dialog);
    QLineEdit* nameEdit = new QLineEdit(&dialog);
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form->addRow("姓名:", nameEdit);
    form->addRow(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        QString claimName = nameEdit->text().trimmed();
        if (claimName != m_loginName) {
            QMessageBox::warning(nullptr, "拒绝", "只能为本人打卡！");
            return;
        }
        if (claimName == m_currentFaceName && m_currentFaceName != "未知访客" && !m_currentFeatureBytes.isEmpty()) {
            QJsonObject req;
            req["type"] = "secure_punch_request";
            req["feature"] = QString(m_currentFeatureBytes.toBase64());
            QJsonObject res = NetworkHelper::request(req);
            if (res["status"].toString() == "success") {
                speakText("考勤指令已加密下发，核验通过");
                QTimer::singleShot(500, this, &PunchModule::loadTodayPunchStatus);
            }
            else {
                QMessageBox::warning(nullptr, "服务端安全拦截", res["msg"].toString());
                // 每次核验失败都上报服务端，由服务端统计阈值
                QJsonObject cheatReq;
                cheatReq["type"] = "punch_cheat";
                cheatReq["name"] = claimName;
                QJsonObject cheatRes = NetworkHelper::request(cheatReq);
                if (cheatRes["status"].toString() == "cheat_recorded") {
                    QMessageBox::critical(nullptr, "安全警告", cheatRes["msg"].toString());
                }
            }
        }
        else {
            // 每次核验不匹配都上报服务端，由服务端判断是否达到作弊阈值
            QJsonObject cheatReq;
            cheatReq["type"] = "punch_cheat";
            cheatReq["name"] = claimName;
            QJsonObject cheatRes = NetworkHelper::request(cheatReq);
            if (cheatRes["status"].toString() == "cheat_recorded") {
                QMessageBox::critical(nullptr, "安全警告", cheatRes["msg"].toString());
            }
            else {
                QMessageBox::warning(nullptr, "失败",
                    QString("人脸核验不匹配！(%1)").arg(cheatRes["msg"].toString()));
            }
        }
    }
}
// 发起异常申诉
void PunchModule::onAppealRequestClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();
    QJsonObject initReq;
    initReq["type"] = "query_approval_candidates";
    initReq["name"] = m_loginName;
    QJsonObject initRes = NetworkHelper::request(initReq);
    if (initRes.isEmpty() || !initRes.contains("my_role")) {
        QMessageBox::critical(parentWidget, "错误", "无法连接服务器获取审批信息，请检查网络！");
        return;
    }
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
    // [Issue #7 修复] 审批链路由服务端计算，客户端只做展示
    QComboBox* appBoxes[] = { app1, app2, app3 };
    QJsonArray chainOrder = initRes["chain_order"].toArray();
    int approvalLevels = initRes["approval_levels"].toInt();
    QMap<QString, QPair<QJsonArray, QString>> sourceMap = {
        {"hr",  {hrArr, "人资经理: "}},
        {"gm",  {gmArr, "总经理: "}},
        {"mgr", {mgrArr, "部门经理: "}}
    };
    for (int i = 0; i < 3; ++i) {
        if (i < chainOrder.size()) {
            QJsonObject step = chainOrder[i].toObject();
            QString src = step["source"].toString();
            QString label = step["label"].toString();
            if (sourceMap.contains(src)) {
                fillFromArray(appBoxes[i], sourceMap[src].first, sourceMap[src].second);
            }
            form->addRow(label + ":", appBoxes[i]);
        }
        else {
            appBoxes[i]->setVisible(false);
        }
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
        NetworkHelper::request(req);
        QMessageBox::information(parentWidget, "成功",
            QString("异常申诉已提交，当前进入 %1 级审批流程。\n审批链：%2")
            .arg(approvalLevels)
            .arg(chainList.join(" → ")));

        QString notifyMsg = QString("%1 提交了考勤异常申诉，请及时审批").arg(m_loginName);
        emit requestSendChat(notifyMsg);
    }
}
// 发起请假申请
void PunchModule::onLeaveRequestClicked()
{
    QWidget* parentWidget = qobject_cast<QWidget*>(this->parent());
    QJsonObject initReq;
    initReq["type"] = "query_approval_candidates";
    initReq["name"] = m_loginName;
    QJsonObject initRes = NetworkHelper::request(initReq);
    if (initRes.isEmpty() || !initRes.contains("my_role")) {
        QMessageBox::critical(parentWidget, "错误", "无法连接服务器获取审批信息，请检查网络连接和服务器状态！");
        return;
    }
    QJsonArray hrArr = initRes["hr_list"].toArray();
    QJsonArray gmArr = initRes["gm_list"].toArray();
    QJsonArray mgrArr = initRes["mgr_list"].toArray();
    // 先选择请假类型
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
    // [Issue #7 修复] 审批链路由服务端计算，客户端只做展示
    QComboBox* appBoxes[] = { app1, app2, app3 };
    QJsonArray chainOrder = initRes["chain_order"].toArray();
    int approvalLevels = initRes["approval_levels"].toInt();
    QMap<QString, QPair<QJsonArray, QString>> sourceMap = {
        {"hr",  {hrArr, "人资经理: "}},
        {"gm",  {gmArr, "总经理: "}},
        {"mgr", {mgrArr, "部门经理: "}}
    };
    for (int i = 0; i < 3; ++i) {
        if (i < chainOrder.size()) {
            QJsonObject step = chainOrder[i].toObject();
            QString src = step["source"].toString();
            QString label = step["label"].toString();
            if (sourceMap.contains(src)) {
                fillFromArray(appBoxes[i], sourceMap[src].first, sourceMap[src].second);
            }
            form->addRow(label + ":", appBoxes[i]);
        }
        else {
            appBoxes[i]->setVisible(false);
        }
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
        NetworkHelper::request(req);
        QMessageBox::information(parentWidget, "成功",
            QString("请假申请已提交，当前进入 %1 级审批流程。\n审批链：%2")
            .arg(approvalLevels)
            .arg(chainList.join(" → ")));
        QString notifyMsg = QString("%1 提交了 [%2] 请假申请，请及时审批")
            .arg(m_loginName, selectedType);
        emit requestSendChat(notifyMsg);
    }
}
// 审批请假申请
void PunchModule::onLeaveApproveClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();
    QDialog apprDlg(parentWidget);
    apprDlg.setWindowTitle("假条审批中心");
    apprDlg.resize(800, 550);
    QVBoxLayout* layout = new QVBoxLayout(&apprDlg);
    QLabel* pendingLabel = new QLabel("待审批");
    pendingLabel->setStyleSheet("font-size:14px; font-weight:bold; color:#409EFF; margin-bottom:4px;");
    layout->addWidget(pendingLabel);
    QTableWidget* table = new QTableWidget(&apprDlg);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({ "申请人", "类型", "开始时间", "理由", "状态", "操作" });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    QJsonObject req;
    req["type"] = "query_pending_leaves";
    req["approver"] = m_loginName;
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        QJsonArray arr = res["data"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject rowObj = arr[i].toObject();
            int row = table->rowCount();
            table->insertRow(row);
            int reqId = rowObj["id"].toInt();
            QString applicant = rowObj["applicant"].toString();
            QString sTime = rowObj["start"].toString();
            QString eTime = rowObj["end"].toString();
            QString lType = rowObj["type"].toString();
            table->setItem(row, 0, new QTableWidgetItem(applicant));
            table->setItem(row, 1, new QTableWidgetItem(lType));
            table->setItem(row, 2, new QTableWidgetItem(sTime));
            table->setItem(row, 3, new QTableWidgetItem(rowObj["reason"].toString()));
            table->setItem(row, 4, new QTableWidgetItem("待审批"));
            // 操作列按钮
            QWidget* opWidget = new QWidget();
            QHBoxLayout* opLayout = new QHBoxLayout(opWidget);
            opLayout->setContentsMargins(2, 2, 2, 2);
            opLayout->setSpacing(4);
            QPushButton* btnPass = new QPushButton("通过");
            btnPass->setStyleSheet("background-color: #67C23A; color: white; border-radius:3px; padding:4px 8px; font-size:12px;");
            QPushButton* btnReject = new QPushButton("驳回");
            btnReject->setStyleSheet("background-color: #F56C6C; color: white; border-radius:3px; padding:4px 8px; font-size:12px;");
            opLayout->addWidget(btnPass);
            opLayout->addWidget(btnReject);
            table->setCellWidget(row, 5, opWidget);
            connect(btnPass, &QPushButton::clicked, [=, &apprDlg]() {
                QJsonObject passReq;
                passReq["type"] = "leave_approve";
                passReq["reqId"] = reqId;
                passReq["applicant"] = applicant;
                passReq["start_time"] = sTime;
                passReq["end_time"] = eTime;
                passReq["leave_type"] = lType;
                passReq["approver"] = m_loginName;
                passReq["action"] = "approve";
                NetworkHelper::request(passReq);
                QMessageBox::information(&apprDlg, "已通过", "请假申请已批准");
                apprDlg.accept();
                });
            connect(btnReject, &QPushButton::clicked, [=, &apprDlg]() {
                bool ok = false;
                QString reason = QInputDialog::getMultiLineText(&apprDlg, "驳回理由",
                    QString("请输入驳回 [%1] 请假申请的理由:").arg(applicant), "", &ok);
                if (!ok || reason.trimmed().isEmpty()) return;
                QJsonObject rejReq;
                rejReq["type"] = "leave_approve";
                rejReq["reqId"] = reqId;
                rejReq["applicant"] = applicant;
                rejReq["approver"] = m_loginName;
                rejReq["action"] = "reject";
                rejReq["reject_reason"] = reason.trimmed();
                NetworkHelper::request(rejReq);
                QMessageBox::information(&apprDlg, "已驳回", "请假申请已被驳回。");
                apprDlg.accept();
                });
        }
    }
    layout->addWidget(table);
    // 已审批记录区域
    QLabel* doneLabel = new QLabel("已审批的记录");
    doneLabel->setStyleSheet("font-size:14px; font-weight:bold; color:#67C23A; margin-top:12px; margin-bottom:4px;");
    layout->addWidget(doneLabel);
    QTableWidget* doneTable = new QTableWidget(&apprDlg);
    doneTable->setColumnCount(6);
    doneTable->setHorizontalHeaderLabels({ "申请人", "请假类型", "时间", "审批链", "当前状态", "驳回理由" });
    doneTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    doneTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    if (res["status"].toString() == "success") {
        QJsonArray doneArr = res["done_data"].toArray();
        for (int i = 0; i < doneArr.size(); ++i) {
            QJsonObject rowObj = doneArr[i].toObject();
            int row = doneTable->rowCount();
            doneTable->insertRow(row);
            doneTable->setItem(row, 0, new QTableWidgetItem(rowObj["applicant"].toString()));
            doneTable->setItem(row, 1, new QTableWidgetItem(rowObj["type"].toString()));
            doneTable->setItem(row, 2, new QTableWidgetItem(rowObj["start"].toString()));
            QString chain = rowObj["approver_chain"].toString().replace("[✓]", "✓").replace("[✗]", "✗");
            doneTable->setItem(row, 3, new QTableWidgetItem(chain));
            QString st = rowObj["status"].toString();
            QTableWidgetItem* stItem = new QTableWidgetItem(st);
            if (st == "已批准") stItem->setForeground(QColor("#67C23A"));
            else if (st == "已驳回") stItem->setForeground(QColor("#F56C6C"));
            else stItem->setForeground(QColor("#E6A23C"));
            doneTable->setItem(row, 4, stItem);
            doneTable->setItem(row, 5, new QTableWidgetItem(rowObj["reject_reason"].toString()));
        }
    }
    layout->addWidget(doneTable);

    apprDlg.exec();
}
// 审批异常申诉
void PunchModule::onAppealApproveClicked() {
    QWidget* parentWidget = (QWidget*)this->parent();
    QDialog apprDlg(parentWidget);
    apprDlg.setWindowTitle("申诉审批中心");
    apprDlg.resize(800, 550);
    QVBoxLayout* layout = new QVBoxLayout(&apprDlg);
    // 待审批区域
    QLabel* pendingLabel = new QLabel("待审批");
    pendingLabel->setStyleSheet("font-size:14px; font-weight:bold; color:#409EFF; margin-bottom:4px;");
    layout->addWidget(pendingLabel);
    QTableWidget* table = new QTableWidget(&apprDlg);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({ "申请人", "异常时间/日期", "申诉类型", "理由", "状态", "操作" });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    QJsonObject req;
    req["type"] = "query_pending_appeals";
    req["approver"] = m_loginName;
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        QJsonArray arr = res["data"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject rowObj = arr[i].toObject();
            int row = table->rowCount();
            table->insertRow(row);
            int reqId = rowObj["id"].toInt();
            QString applicant = rowObj["applicant"].toString();
            QString aTime = rowObj["time"].toString();
            QString aType = rowObj["type"].toString();
            table->setItem(row, 0, new QTableWidgetItem(applicant));
            table->setItem(row, 1, new QTableWidgetItem(aTime));
            table->setItem(row, 2, new QTableWidgetItem(aType));
            table->setItem(row, 3, new QTableWidgetItem(rowObj["reason"].toString()));
            table->setItem(row, 4, new QTableWidgetItem("待审批"));
            QWidget* opWidget = new QWidget();
            QHBoxLayout* opLayout = new QHBoxLayout(opWidget);
            opLayout->setContentsMargins(2, 2, 2, 2);
            opLayout->setSpacing(4);
            QPushButton* btnPass = new QPushButton("通过");
            btnPass->setStyleSheet("background-color: #67C23A; color: white; border-radius:3px; padding:4px 8px; font-size:12px;");
            QPushButton* btnReject = new QPushButton("驳回");
            btnReject->setStyleSheet("background-color: #F56C6C; color: white; border-radius:3px; padding:4px 8px; font-size:12px;");
            opLayout->addWidget(btnPass);
            opLayout->addWidget(btnReject);
            table->setCellWidget(row, 5, opWidget);
            connect(btnPass, &QPushButton::clicked, [=, &apprDlg]() {
                QJsonObject passReq;
                passReq["type"] = "appeal_approve";
                passReq["reqId"] = reqId;
                passReq["applicant"] = applicant;
                passReq["abnormal_time"] = aTime;
                passReq["appeal_type"] = aType;
                passReq["approver"] = m_loginName;
                passReq["action"] = "approve";
                NetworkHelper::request(passReq);
                QMessageBox::information(&apprDlg, "已通过", "申诉已批准");
                apprDlg.accept();
                });
            connect(btnReject, &QPushButton::clicked, [=, &apprDlg]() {
                bool ok = false;
                QString reason = QInputDialog::getMultiLineText(&apprDlg, "驳回理由",
                    QString("请输入驳回 [%1] 申诉的理由:").arg(applicant), "", &ok);
                if (!ok || reason.trimmed().isEmpty()) return;
                QJsonObject rejReq;
                rejReq["type"] = "appeal_approve";
                rejReq["reqId"] = reqId;
                rejReq["applicant"] = applicant;
                rejReq["approver"] = m_loginName;
                rejReq["action"] = "reject";
                rejReq["reject_reason"] = reason.trimmed();
                NetworkHelper::request(rejReq);
                QMessageBox::information(&apprDlg, "已驳回", "申诉已被驳回");
                apprDlg.accept();
                });
        }
    }
    layout->addWidget(table);
    // 已审批记录区域
    QLabel* doneLabel = new QLabel("已审批记录");
    doneLabel->setStyleSheet("font-size:14px; font-weight:bold; color:#67C23A; margin-top:12px; margin-bottom:4px;");
    layout->addWidget(doneLabel);
    QTableWidget* doneTable = new QTableWidget(&apprDlg);
    doneTable->setColumnCount(6);
    doneTable->setHorizontalHeaderLabels({ "申请人", "申诉类型", "理由", "审批链", "当前状态", "驳回理由" });
    doneTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    doneTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    if (res["status"].toString() == "success") {
        QJsonArray doneArr = res["done_data"].toArray();
        for (int i = 0; i < doneArr.size(); ++i) {
            QJsonObject rowObj = doneArr[i].toObject();
            int row = doneTable->rowCount();
            doneTable->insertRow(row);
            doneTable->setItem(row, 0, new QTableWidgetItem(rowObj["applicant"].toString()));
            doneTable->setItem(row, 1, new QTableWidgetItem(rowObj["type"].toString()));
            doneTable->setItem(row, 2, new QTableWidgetItem(rowObj["reason"].toString()));
            QString chain = rowObj["approver_chain"].toString().replace("[✓]", "✓").replace("[✗]", "✗");
            doneTable->setItem(row, 3, new QTableWidgetItem(chain));
            QString st = rowObj["status"].toString();
            QTableWidgetItem* stItem = new QTableWidgetItem(st);
            if (st == "已批准") stItem->setForeground(QColor("#67C23A"));
            else if (st == "已驳回") stItem->setForeground(QColor("#F56C6C"));
            else stItem->setForeground(QColor("#E6A23C"));
            doneTable->setItem(row, 4, stItem);
            doneTable->setItem(row, 5, new QTableWidgetItem(rowObj["reject_reason"].toString()));
        }
    }
    layout->addWidget(doneTable);
    apprDlg.exec();
}
// 更新时间显示
void PunchModule::onTimeUpdate() {
    if (m_lblCurrentTime) m_lblCurrentTime->setText("当前时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
}
// 渲染摄像头画面
void PunchModule::renderFrame(const QImage& img) {
    if (m_cameraLabel && !img.isNull()) m_cameraLabel->setPixmap(QPixmap::fromImage(img).scaled(m_cameraLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
// 保存当前人脸特征
void PunchModule::updateCurrentFaceFeature(const QByteArray& featureBytes) {
    m_currentFeatureBytes = featureBytes;
}
// 保存当前识别姓名
void PunchModule::updateRecognizedName(const QString& name) {
    m_currentFaceName = name;
}
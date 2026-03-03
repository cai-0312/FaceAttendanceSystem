#pragma once
#include <QObject>
#include <QLabel>
#include <QImage>
#include <QPushButton>
#include <QTime>
#include <QTimer>

class PunchModule : public QObject {
    Q_OBJECT
public:
    // 构造函数，完美对应 13 个参数
    explicit PunchModule(QLabel* cameraLabel, QPushButton* manualBtn,
        QLabel* morningTime, QLabel* morningStatus,
        QLabel* eveningTime, QLabel* eveningStatus,
        QPushButton* ruleBtn,
        QPushButton* leaveReqBtn, QPushButton* leaveApprBtn,
        QLabel* currentTimeLabel,
        QString role, QString loginName, QObject* parent = nullptr);

    void renderFrame(const QImage& img);
    void updateRecognizedName(const QString& name);
    QString calculatePunchStatus(const QTime& punchTime);
    void loadTodayPunchStatus();

signals:
    // 请求聊天模块代发消息的信号
    void requestSendChat(const QString& target, const QString& msg);

private slots:
    void onManualPunchClicked();
    void onRuleSettingsClicked();
    void onTimeUpdate();
    void onLeaveRequestClicked(); // 申请请假
    void onLeaveApproveClicked(); // 审批请假

private:
    void initRulesTable();
    void loadRules();

private:
    QLabel* m_cameraLabel;
    QPushButton* m_manualBtn;

    QLabel* m_lblMorningTime;
    QLabel* m_lblMorningStatus;
    QLabel* m_lblEveningTime;
    QLabel* m_lblEveningStatus;
    QPushButton* m_btnRuleSettings;

    QPushButton* m_btnLeaveRequest;
    QPushButton* m_btnLeaveApprove;
    QLabel* m_lblCurrentTime;

    QString m_currentFaceName;
    int m_cheatCount;
    QString m_role;
    QString m_loginName;

    QTime m_startTime;
    QTime m_endTime;
    int m_lateMins;
    int m_absentMins;
    QTimer* m_timer;
};
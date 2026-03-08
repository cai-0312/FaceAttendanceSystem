#ifndef PUNCHMODULE_H
#define PUNCHMODULE_H

#include <QObject>
#include <QLabel>
#include <QPushButton>
#include <QTime>
#include <QTimer>
#include <QImage>

class PunchModule : public QObject
{
    Q_OBJECT
public:
    PunchModule(QLabel* cameraLabel, QPushButton* manualBtn,
        QLabel* morningTime, QLabel* morningStatus,
        QLabel* eveningTime, QLabel* eveningStatus,
        QPushButton* ruleBtn, QPushButton* leaveReqBtn, QPushButton* leaveApprBtn,
        QPushButton* appealReqBtn, QPushButton* appealApprBtn,
        QLabel* currentTimeLabel,
        QString role, QString loginName, QObject* parent = nullptr);

    void renderFrame(const QImage& img);
    void updateRecognizedName(const QString& name);
    void initRulesTable();

    // 🚀 核心修复 1：将这些函数从 private 移动到 public，让 MainWidget 可以成功 connect
public slots: // 🚀 必须改在这里！
    void onLeaveRequestClicked();
    void onLeaveApproveClicked();
    void onAppealRequestClicked();
    void onAppealApproveClicked();
    void onManualPunchClicked();
    void onRuleSettingsClicked();
    void loadTodayPunchStatus();
    void onTimeUpdate();

signals:
    void requestSendChat(QString msg);

private:
    // 🚀 核心修复 2：参数声明必须匹配 .cpp，解决“参数太多”报错
    void loadRules(QString myDept = "");
    void speakText(const QString& text);
    QString calculatePunchStatus(const QTime& punchTime);

    QLabel* m_cameraLabel;
    QPushButton* m_manualBtn;
    QLabel* m_lblMorningTime;
    QLabel* m_lblMorningStatus;
    QLabel* m_lblEveningTime;
    QLabel* m_lblEveningStatus;
    QPushButton* m_btnRuleSettings;
    QPushButton* m_btnLeaveRequest;
    QPushButton* m_btnLeaveApprove;
    QPushButton* m_btnAppealRequest;
    QPushButton* m_btnAppealApprove;
    QLabel* m_lblCurrentTime;

    QString m_role;
    QString m_loginName;
    QString m_currentFaceName;

    QString m_shiftName;
    QTime m_startTime;
    QTime m_endTime;
    int m_lateMins;
    int m_absentMins;

    int m_cheatCount;
    QTimer* m_timer;
};

#endif // PUNCHMODULE_H
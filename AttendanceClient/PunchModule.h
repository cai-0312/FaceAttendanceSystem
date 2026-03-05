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
    explicit PunchModule(QLabel* cameraLabel, QPushButton* manualBtn,
        QLabel* morningTime, QLabel* morningStatus,
        QLabel* eveningTime, QLabel* eveningStatus,
        QPushButton* ruleBtn,
        QPushButton* leaveReqBtn, QPushButton* leaveApprBtn,
        QPushButton* appealReqBtn, QPushButton* appealApprBtn,
        QLabel* currentTimeLabel,
        QString role, QString loginName, QObject* parent = nullptr);

    // 渲染视频帧：将摄像头捕获的画面显示到指定的标签控件上
    void renderFrame(const QImage& img);
    // 更新当前识别到的姓名：用于打卡时的身份核验
    void updateRecognizedName(const QString& name);
    // 计算打卡状态：根据当前时间判断是正常、迟到、早退还是旷工
    QString calculatePunchStatus(const QTime& punchTime);
    // 加载今日打卡记录：从数据库查询当天的考勤状态并更新界面
    void loadTodayPunchStatus();

signals:
    // 发送消息请求信号：用于触发即时通讯模块发送审批通知
    void requestSendChat(const QString& target, const QString& msg);

private slots:
    // 手动打卡按钮点击处理槽函数
    void onManualPunchClicked();
    // 考勤规则设置按钮点击处理槽函数
    void onRuleSettingsClicked();
    // 界面时间刷新定时器槽函数
    void onTimeUpdate();
    // 发起请假申请按钮点击处理槽函数
    void onLeaveRequestClicked();
    // 审批请假申请按钮点击处理槽函数
    void onLeaveApproveClicked();
    // 发起考勤申诉按钮点击处理槽函数
    void onAppealRequestClicked();
    // 审批考勤申诉按钮点击处理槽函数
    void onAppealApproveClicked();

private:
    // 初始化数据库考勤规则表与结构
    void initRulesTable();
    // 从数据库加载当前班次的时间规则
    void loadRules();
    // 语音播报：调用系统底层接口实现文字转语音提醒
    void speakText(const QString& text);

private:
    // UI 控件成员指针
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

    // 运行状态数据成员
    QString m_currentFaceName;  // 当前摄像头识别出的人员姓名
    int m_cheatCount;           // 防作弊计数器
    QString m_role;             // 当前登录用户的角色权限
    QString m_loginName;        // 当前登录用户的用户名

    QString m_shiftName;        // 当前所属班次名称（如常规班、早班等）

    // 考勤时间判定参数
    QTime m_startTime;          // 规定上班时间
    QTime m_endTime;            // 规定下班时间
    int m_lateMins;             // 允许迟到的分钟数
    int m_absentMins;           // 判定旷工的分钟数阈值
    QTimer* m_timer;            // 用于界面实时时钟刷新的定时器
};
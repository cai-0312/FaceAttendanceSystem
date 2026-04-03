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
    // 初始化打卡模块并绑定界面控件
    PunchModule(QLabel* cameraLabel, QPushButton* manualBtn, QLabel* morningTime, QLabel* morningStatus, QLabel* eveningTime, QLabel* eveningStatus, QPushButton* ruleBtn, QPushButton* leaveReqBtn, QPushButton* leaveApprBtn, QPushButton* appealReqBtn, QPushButton* appealApprBtn, QLabel* currentTimeLabel, QString role, QString loginName, QObject* parent = nullptr); 
    void renderFrame(const QImage& img); // 渲染视频帧到界面
    void updateRecognizedName(const QString& name); // 更新当前识别到的人脸姓名
    void initRulesTable(); // 初始化考勤规则表
    void updateCurrentFaceFeature(const QByteArray& featureBytes); // 更新当前人脸特征数据
    QByteArray getCurrentFeatureBytes() const { return m_currentFeatureBytes; } // 获取当前人脸特征数据
public slots:
    void onLeaveRequestClicked(); // 打开请假申请流程
    void onLeaveApproveClicked(); // 打开请假审批流程
    void onAppealRequestClicked(); // 打开异常申诉流程
    void onAppealApproveClicked(); // 打开异常申诉审批流程
    void onManualPunchClicked(); // 执行手动打卡
    void onRuleSettingsClicked(); // 打开考勤规则设置
    void loadTodayPunchStatus(); // 加载当天打卡状态
    void onTimeUpdate(); // 刷新当前时间显示
signals:
    void requestSendChat(QString msg); // 发送系统提示消息
private:
    void loadRules(QString myDept = ""); // 加载当前部门的考勤规则
    void speakText(const QString& text); // 语音播报打卡结果
    QString calculatePunchStatus(const QTime& punchTime); // 计算打卡状态
    QLabel* m_cameraLabel; // 视频显示控件
    QPushButton* m_manualBtn; // 手动打卡按钮
    QLabel* m_lblMorningTime; // 早班标准时间
    QLabel* m_lblMorningStatus; // 早班打卡状态
    QLabel* m_lblEveningTime; // 晚班标准时间
    QLabel* m_lblEveningStatus; // 晚班打卡状态
    QPushButton* m_btnRuleSettings; // 规则设置按钮
    QPushButton* m_btnLeaveRequest; // 请假申请按钮
    QPushButton* m_btnLeaveApprove; // 请假审批按钮
    QPushButton* m_btnAppealRequest; // 申诉申请按钮
    QPushButton* m_btnAppealApprove; // 申诉审批按钮
    QLabel* m_lblCurrentTime; // 当前时间显示控件
    QString m_role; // 当前用户角色
    QString m_loginName; // 当前登录账号
    QString m_currentFaceName; // 当前识别到的人脸姓名
    QString m_shiftName; // 当前排班名称
    QTime m_startTime; // 上班时间
    QTime m_endTime; // 下班时间
    int m_lateMins; // 允许迟到分钟数
    int m_absentMins; // 判定旷工分钟数
    int m_cheatCount; // 连续异常次数
    QTimer* m_timer; // 时间刷新定时器
    QByteArray m_currentFeatureBytes; // 当前人脸特征缓存
};
#endif 
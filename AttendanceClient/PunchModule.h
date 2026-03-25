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
    // 构造函数：初始化打卡业务模块并绑定全部对应的界面控件资源
    PunchModule(QLabel* cameraLabel, QPushButton* manualBtn, QLabel* morningTime, QLabel* morningStatus, QLabel* eveningTime, QLabel* eveningStatus, QPushButton* ruleBtn, QPushButton* leaveReqBtn, QPushButton* leaveApprBtn, QPushButton* appealReqBtn, QPushButton* appealApprBtn, QLabel* currentTimeLabel, QString role, QString loginName, QObject* parent = nullptr); 
    void renderFrame(const QImage& img);                                          // 视频流渲染：接收底层线程处理完毕的图像帧并更新至UI展示组件
    void updateRecognizedName(const QString& name);                                // 人脸状态同步：接收底层识别计算出的人员姓名，用于辅助后续的打卡防抖判断
    void initRulesTable();                                                         // 初始化方法：预留的排班规则表初始化扩展接口
    void updateCurrentFaceFeature(const QByteArray& featureBytes); //接收底层算法传递的特征字节流// 暴露当前缓存的人脸特征给外部网络发包使用
    QByteArray getCurrentFeatureBytes() const { return m_currentFeatureBytes; }
public slots:
    void onLeaveRequestClicked();                                                  // 界面交互：响应快捷请假按钮，弹出申请表单并校验审批流
    void onLeaveApproveClicked();                                                  // 界面交互：响应请假审批按钮，拉取待办列表并执行审批授权
    void onAppealRequestClicked();                                                 // 界面交互：响应异常申诉按钮，查询异常记录并提交申诉单据
    void onAppealApproveClicked();                                              // 界面交互：响应申诉审批按钮，加载申诉列表并下发修正指令
    void onManualPunchClicked();                                              // 界面交互：响应手动打卡按钮，结合当前人脸状态执行辅助打卡与安全记录
    void onRuleSettingsClicked();                                             // 界面交互：响应排班规则按钮，弹出动态表单修改对应部门的考勤标准
    void loadTodayPunchStatus();                                              // 数据加载：向服务端拉取当前用户今日所有的打卡流水并进行考勤状态渲染
    void onTimeUpdate();                                                      // 定时器回调：驱动界面顶部实时时钟的秒级刷新显示
signals:
    void requestSendChat(QString msg);                                         // 通讯信号：向外部环境抛出文本事件，联动即时通讯模块发送系统提示
private:
    void loadRules(QString myDept = "");                                          // 业务逻辑：根据用户所属部门，向服务端请求对应的上下班基准时间与判定宽限
    void speakText(const QString& text);                                           // 语音控制：调度底层操作系统的语音合成接口进行打卡结果播报
    QString calculatePunchStatus(const QTime& punchTime);                          // 状态判定：比对真实打卡时间与排班基准，计算返回早退、迟到或正常的枚举字符串
    QLabel* m_cameraLabel;                                                         // 控件指针：承载人脸实时视频流的标签组件
    QPushButton* m_manualBtn;                                               // 控件指针：触发手动打卡逻辑的操作按钮
    QLabel* m_lblMorningTime;                                              // 控件指针：展示早班标准打卡时间的标签
    QLabel* m_lblMorningStatus;                                           // 控件指针：展示早班真实打卡及缺卡状态的标签
    QLabel* m_lblEveningTime;                                             // 控件指针：展示晚班标准打卡时间的标签
    QLabel* m_lblEveningStatus;                                          // 控件指针：展示晚班真实打卡及缺卡状态的标签
    QPushButton* m_btnRuleSettings;                                     // 控件指针：触发考勤规则面板的入口按钮
    QPushButton* m_btnLeaveRequest;                                      // 控件指针：触发请假申请流的入口按钮
    QPushButton* m_btnLeaveApprove;                                     // 控件指针：进入请假审批工作台的入口按钮
    QPushButton* m_btnAppealRequest;                                     // 控件指针：触发异常申诉流的入口按钮
    QPushButton* m_btnAppealApprove;                                    // 控件指针：进入异常申诉审批工作台的入口按钮
    QLabel* m_lblCurrentTime;                                           // 控件指针：展示系统实时时间的文本标签
    QString m_role;                                                    // 核心变量：当前登录系统的用户权限角色定义
    QString m_loginName;                                              // 核心变量：当前登录系统的用户唯一识别名称
    QString m_currentFaceName;                                         // 核心变量：目前视频画面中锁定识别的有效人脸名
    QString m_shiftName;                                             // 规则变量：当前挂载生效的排班策略名称
    QTime m_startTime;                                                // 规则变量：排班策略设定的基准上班时间
    QTime m_endTime;                                                  // 规则变量：排班策略设定的基准下班时间
    int m_lateMins;                                                  // 规则变量：允许迟到的宽容分钟阈值
    int m_absentMins;                                                // 规则变量：判定为旷工的严重迟到分钟阈值
    int m_cheatCount;                                                // 安全变量：记录因人脸不匹配造成的连续异常请求次数
    QTimer* m_timer;                                                // 核心变量：用于驱动界面时钟更新的循环定时器
    QByteArray m_currentFeatureBytes;                               // 缓存当前画面中锁定人脸的特征向量
};
#endif // PUNCHMODULE_H
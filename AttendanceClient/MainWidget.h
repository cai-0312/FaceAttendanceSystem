#ifndef MAINWIDGET_H
#define MAINWIDGET_H
#include <QWidget>
#include <QMap>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "FaceProcessThread.h"
#include "RecordModule.h"
#include "PunchModule.h"
#include "UserModule.h"
#include "RegisterModule.h"
#include "ProfileModule.h"
#include "ChatModule.h"
#include "AIAssistantModule.h"
#include "HomeModule.h"
#include "NoticePopup.h"
#include <QTextEdit>
#include "RetinaFaceDecoder.h"
namespace Ui { class MainWidget; }
struct PunchState {                                             // 考勤状态结构体用于记录防抖状态
    QDateTime lastPunchTime;                                   // 记录上一次成功触发打卡判定的时间
    int punchCount = 0;                                       // 记录连续识别成功的帧数累加器
    PunchState() : punchCount(0) {}                          // 显式声明构造函数以满足编译要求
};
class MainWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MainWidget(QString loginName, QString role, QWidget* parent = nullptr);             // 构造函数：初始化主界面与各业务模块
    ~MainWidget();                                                               // 析构函数：释放主界面对象树及后台线程资源
protected:
    void closeEvent(QCloseEvent* event) override;                              // 拦截窗口关闭事件，确保摄像头与线程安全退出
private slots:
    void onStatusChanged(const QString& status);                              // 状态变更槽函数，向上位机同步当前用户在线状态
private:
    void loadRegisteredUsers();                                           // 从服务端拉取全量人脸特征库并同步至识别线程
    Ui::MainWidget* ui;                                                  // 主界面UI设计器生成的指针句柄
    QString m_loginName;                                                     // 记录当前登录系统的账户名
    QString m_role;                                                        // 记录当前登录系统的权限角色
    FaceProcessThread* aiThread;                                      // 人脸检测与特征提取后台工作线程
    RecordModule* recordModule;                                       // 考勤记录检索与导出业务模块
    PunchModule* punchModule;                                           // 人脸打卡结果判定与UI展示模块
    UserModule* userModule;                                       // 企业员工花名册管理业务模块
    RegisterModule* registerModule;                         // 员工人脸特征采集与注册入库模块
    ProfileModule* profileModule;                       // 个人信息档案展示与设置模块
    ChatModule* chatModule;                                          // 局域网即时通讯与文件分发模块
    AIAssistantModule* m_aiModule;                                    // 基于大语言模型的智能考勤问答模块
    HomeModule* homeModule;                                          // 考勤数据可视化大屏展示模块
    QNetworkAccessManager* m_netManager;                            // 统一的HTTP网络通信资源管理器
    std::map<QString, PunchState> m_punchStates;                   // 内存状态字典，防止由于高帧率造成的频繁打卡请求
};
#endif // MAINWIDGET_H
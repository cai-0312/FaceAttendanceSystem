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
class AttendanceClient;
namespace Ui { class MainWidget; }
struct PunchState
{
    QDateTime lastPunchTime; // 上次触发打卡判定的时间
    int punchCount = 0; // 连续识别成功的帧数
    PunchState() : punchCount(0) {} // 默认初始化识别计数
};
class MainWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MainWidget(QString loginName, QString role, QWidget* parent = nullptr,
        AttendanceClient* loginWindow = nullptr); // 初始化主界面并接收登录信息
    ~MainWidget(); // 释放主界面资源
    void forceNavigateTo(int navIndex); // 强制跳转到指定导航页
protected:
    void closeEvent(QCloseEvent* event) override; // 拦截关闭事件并安全退出
private slots:
    void onStatusChanged(const QString& status); // 同步当前用户在线状态
private:
    void loadRegisteredUsers(); // 从服务端加载人脸特征库
    Ui::MainWidget* ui; // 界面设计器生成的指针
    QString m_loginName; // 当前登录账号
    QString m_role; // 当前登录角色
    FaceProcessThread* aiThread; // 人脸检测与特征提取线程
    RecordModule* recordModule; // 考勤记录模块
    PunchModule* punchModule; // 打卡判定模块
    UserModule* userModule; // 员工管理模块
    RegisterModule* registerModule; // 人脸注册模块
    ProfileModule* profileModule; // 个人档案模块
    ChatModule* chatModule; // 即时通讯模块
    AIAssistantModule* m_aiModule; // 智能问答模块
    HomeModule* homeModule; // 首页大屏模块
    QNetworkAccessManager* m_netManager; // 网络请求管理器
    AttendanceClient* m_loginWindow = nullptr; // 登录窗口指针
    std::map<QString, PunchState> m_punchStates; // 打卡防抖状态表
};
#endif // MAINWIDGET_H
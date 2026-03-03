#pragma once
#include <QWidget>
#include <QImage>
#include <QDateTime>
#include <map>
#include "AIAssistantModule.h"

// 前向声明各个独立模块
class FaceProcessThread;
class RecordModule;
class PunchModule;
class UserModule;
class RegisterModule;
class ProfileModule;
class ChatModule;
class AIAssistantModule;

namespace Ui { class MainWidget; }
// 🚀 新增：打卡风控状态追踪器
struct PunchState {
    QDateTime lastPunchTime; // 最后一次打卡的时间
    int punchCount = 0;      // 记录短时间内的打卡次数
};
class MainWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MainWidget(QString loginName, QString role, QWidget* parent = nullptr);
    ~MainWidget();

private slots:
    void onBtnChangeAvatarClicked(); // 点击更换头像按钮

private:
    Ui::MainWidget* ui;

    // 五大金刚模块
	FaceProcessThread* aiThread; //人脸处理线程
    RecordModule* recordModule; //考勤记录
    PunchModule* punchModule; //考勤打卡
    UserModule* userModule; //员工管理
    RegisterModule* registerModule; //人脸录入
	ProfileModule* profileModule; //个人信息
	ChatModule* chatModule; //聊天
	AIAssistantModule* aIAssistantModule; // AI 助手

    std::map<QString, PunchState> m_punchStates;

    void initDatabase();
    void loadRegisteredUsers();

    QString m_loginName;
    QString m_role;

    void loadProfileInfo(); // 加载个人资料页数据
    QImage makeCircularAvatar(const QImage& src, int size); // 图像裁剪抗锯齿算法
};


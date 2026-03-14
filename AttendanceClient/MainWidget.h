#pragma once
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

// 考勤状态结构体
struct PunchState {
    QDateTime lastPunchTime;
    int punchCount = 0;
};

class MainWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MainWidget(QString loginName, QString role, QWidget* parent = nullptr);
    ~MainWidget();

private slots:
    void onStatusChanged(const QString& status);

private:
    void loadRegisteredUsers();

private:
    Ui::MainWidget* ui;
    QString m_loginName;
    QString m_role;

    FaceProcessThread* aiThread;
    RecordModule* recordModule;
    PunchModule* punchModule;
    UserModule* userModule;
    RegisterModule* registerModule;
    ProfileModule* profileModule;
    ChatModule* chatModule;
    AIAssistantModule* m_aiModule;
    HomeModule* homeModule;

    QNetworkAccessManager* m_netManager;
    QMap<QString, PunchState> m_punchStates;

protected:
    void closeEvent(QCloseEvent* event) override;
};
#pragma once
#include <QObject>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

class AIAssistantModule : public QObject {
    Q_OBJECT
public:
    explicit AIAssistantModule(QTextBrowser* textBrowser,
        QLineEdit* lineEdit,
        QPushButton* sendBtn,
        QPushButton* clearBtn,
        QString userName,
        QObject* parent = nullptr);

private slots:
    // 发送按钮点击及回车触发的槽函数
    void onSendClicked();
    // 网络请求完成后的回调处理槽函数
    void onNetworkReply(QNetworkReply* reply);
    // 清除对话记忆与界面内容的槽函数
    void onClearClicked();

private:
    // 将对话消息渲染为HTML气泡并添加到显示区域
    void appendMessage(const QString& role, const QString& msg);
    // 初始化对话上下文及系统提示词
    void initializeContext();

    // 界面控件指针
    QTextBrowser* m_textBrowser;
    QLineEdit* m_lineEdit;
    QPushButton* m_sendBtn;
    QPushButton* m_clearBtn;

    // 网络访问管理器
    QNetworkAccessManager* m_networkManager;

    // 用户与API配置信息
    QString m_userName;
    QString m_apiUrl;
    QString m_apiKey;
    QString m_modelName;

    // 对话历史记忆数组（用于多轮对话上下文）
    QJsonArray m_messageHistory;
    // 当前界面显示的全部HTML内容
    QString m_currentHtmlDisplay;
    // 标志位：判断当前是否处于AI回复等待状态
    bool m_isReplying;
};
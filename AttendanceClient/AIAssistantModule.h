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
    void onSendClicked();
    void onNetworkReply(QNetworkReply* reply);
    void onClearClicked(); // ★ 清除记忆的槽函数

private:
    void appendMessage(const QString& role, const QString& msg);
    void initializeContext(); // 初始化并重置对话

    QTextBrowser* m_textBrowser;
    QLineEdit* m_lineEdit;
    QPushButton* m_sendBtn;
    QPushButton* m_clearBtn;
    QNetworkAccessManager* m_networkManager;

    QString m_userName;
    QString m_apiUrl;
    QString m_apiKey;
    QString m_modelName;

    QJsonArray m_messageHistory;
    QString m_currentHtmlDisplay;
    bool m_isReplying;
};
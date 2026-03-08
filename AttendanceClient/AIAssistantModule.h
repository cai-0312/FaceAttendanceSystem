#ifndef AIASSISTANTMODULE_H
#define AIASSISTANTMODULE_H

#include <QObject>
#include <QTextBrowser>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>
#include <QTcpSocket>
#include <QList>
#include <QPair>

class AIAssistantModule : public QObject
{
    Q_OBJECT
public:
    AIAssistantModule(QTextBrowser* textBrowser,
        QLineEdit* lineEdit,
        QPushButton* sendBtn,
        QPushButton* clearBtn,
        QString userName,
        QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onSendClicked();
    void onNetworkReply(QNetworkReply* reply);
    void toggleVoice();

    void onNewSessionClicked();
    void onSessionSelected(QListWidgetItem* item);
    void onAttachFileClicked();
    void onSearchHistory();

    void toggleSidebar();
    void onSessionContextMenu(const QPoint& pos);

private:
    void initializeContext();
    void appendMessage(const QString& role, const QString& msg, bool saveToDb = true);

    void rebuildAdvancedUI();
    bool handleLocalIntent(const QString& inputText);
    QString parseMarkdown(const QString& md);
    void speakText(const QString& text);

    void loadSessionsFromDB();
    void loadChatHistoryFromDB(const QString& sessionId);
    void saveMessageToDB(const QString& sessionId, const QString& role, const QString& content);

    // 🚀 服务端探针
    void sendAuditToServer(const QString& sessionId, const QString& role, const QString& content);
    void sendAuditFileToServer(const QString& sessionId, const QString& fileName, const QByteArray& fileData); // 🚀 新增：拦截原文件探针

    // 控件指针
    QTextBrowser* m_textBrowser;
    QLineEdit* m_oldLineEdit;
    QPushButton* m_sendBtn;
    QPushButton* m_clearBtn;
    QPushButton* m_voiceBtn;

    QWidget* m_leftWidget;
    QPushButton* m_toggleSidebarBtn;
    QListWidget* m_sessionList;
    QPushButton* m_newSessionBtn;
    QPushButton* m_attachBtn;
    QLineEdit* m_searchBox;
    QTextEdit* m_inputTextEdit;

    QNetworkAccessManager* m_networkManager;
    QString m_apiUrl;
    QString m_apiKey;
    QString m_modelName;

    QJsonArray m_messageHistory;
    QString m_currentHtmlDisplay;
    QString m_userName;
    QString m_currentSessionId;
    QString m_fileContext;

    // 🚀 新增：暂存用户选择的物理附件，等点发送时一并传给服务器
    QList<QPair<QString, QByteArray>> m_pendingFiles;

    bool m_isReplying;
    bool m_voiceEnabled;
};

#endif // AIASSISTANTMODULE_H
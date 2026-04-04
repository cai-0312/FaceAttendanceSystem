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
#include <QMap>
class AIAssistantModule : public QObject
{
    Q_OBJECT
public:
    // 构造并绑定 AI 助手的 UI 控件
    AIAssistantModule(QTextBrowser* textBrowser,QLineEdit* lineEdit,QPushButton* sendBtn,QPushButton* clearBtn,QString userName,QObject* parent = nullptr); 
public slots:
    void clearCurrentSession(); // 清空当前会话上下文
protected:
    bool eventFilter(QObject* obj, QEvent* event) override; // 事件过滤用于拦截回车等输入行为
private slots:
    void onSendClicked();                                     // 发送按钮点击处理
    void handleAiResponse(const QJsonObject& json);  // 处理服务端返回的AI回复
    void toggleVoice();                                     // 切换语音播报开关
    void onNewSessionClicked();                           // 新建会话处理
    void onSessionSelected(QListWidgetItem* item);       // 选择历史会话时加载会话
    void onAttachFileClicked();                        // 点击附件按钮处理上传
    void onSearchHistory();                           // 搜索历史消息
    void toggleSidebar();                            // 切换侧边栏显示状态
    void onSessionContextMenu(const QPoint& pos);   // 会话列表右键菜单处理
    void onAnchorClicked(const QUrl& url);           // 处理聊天气泡中的链接点击（图片保存/外部链接）
private:
    void initializeContext();                                      // 初始化对话上下文与系统提示词
    void appendMessage(const QString& role, const QString& msg, bool saveToDb = true); // 渲染并追加消息到界面
    void rebuildAdvancedUI();                                      // 重建复杂 UI 布局
    bool handleLocalIntent(const QString& inputText);              // 处理本地意图如快速查询
    QString parseMarkdown(const QString& md);                      // 将简单 Markdown 转为 HTML
    void speakText(const QString& text);                           // 将文本通过系统 TTS 播报
    void loadSessionsFromDB();                                     // 加载本地/服务端保存的会话列表
    void loadChatHistoryFromDB(const QString& sessionId);          // 加载指定会话的历史消息
    void saveMessageToDB(const QString& sessionId, const QString& role, const QString& content); // 异步保存聊天消息到数据库
    // 服务端审计上报
    void sendAuditToServer(const QString& sessionId, const QString& role, const QString& content); // 将聊天文本发送到服务端审计
    void sendAuditFileToServer(const QString& sessionId, const QString& fileName, const QByteArray& fileData); // 上传文件用于服务端审计
    void downloadAndDisplayImage(const QString& imgUrl); // 下载 AI 生成的图片并内联展示到聊天界面
    // 控件
    QTextBrowser* m_textBrowser;    // 消息显示控件
    QLineEdit* m_oldLineEdit;       // 备用单行输入框
    QPushButton* m_sendBtn;         // 发送按钮
    QPushButton* m_clearBtn;        // 清空按钮
    QPushButton* m_voiceBtn;        // 语音播报切换按钮
    QWidget* m_leftWidget;          // 左侧侧栏容器
    QPushButton* m_toggleSidebarBtn;  // 侧栏切换按钮
    QListWidget* m_sessionList;        // 会话列表控件
    QPushButton* m_newSessionBtn;     // 新建会话按钮
    QPushButton* m_attachBtn;         // 附件上传按钮
    QLineEdit* m_searchBox;            // 会话搜索输入框
    QTextEdit* m_inputTextEdit;        // 多行输入框
    QString m_modelName;                     // 当前模型名称
    QJsonArray m_messageHistory;           // 当前会话上下文消息数组
    QString m_currentHtmlDisplay;          // 当前聊天窗口渲染的 HTML
    QString m_userName;                  // 本地用户名
    QString m_currentSessionId;          // 当前会话 ID
    QString m_fileContext;                // 附件解析后的上下文文本
    QList<QPair<QString, QByteArray>> m_pendingFiles;     // 待上传的文件队列
    bool m_isReplying;                               // 标记是否正在等待回复
    bool m_voiceEnabled;                             // 语音播报是否启用
    QNetworkAccessManager* m_netManager = nullptr;   // 用于下载 AI 生成的图片
};
#endif // AIASSISTANTMODULE_H
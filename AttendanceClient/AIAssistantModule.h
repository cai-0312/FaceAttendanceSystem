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
    // 初始化AI助手模块并绑定UI控件
    AIAssistantModule(QTextBrowser* textBrowser,QLineEdit* lineEdit,QPushButton* sendBtn,QPushButton* clearBtn,QString userName,QObject* parent = nullptr);        
public slots:
    void clearCurrentSession();
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;       // 事件过滤器，用于拦截输入框的按键事件（如Enter发送）
private slots:
    void onSendClicked();                                     // 处理发送按钮点击事件
    void onNetworkReply(QNetworkReply* reply);               // 处理网络请求完成后的响应数据
    void toggleVoice();                                     // 切换语音播报功能的开关状态
    void onNewSessionClicked();                           // 处理新建对话按钮点击事件
    void onSessionSelected(QListWidgetItem* item);       // 处理历史会话列表项的选择事件
    void onAttachFileClicked();                        // 处理上传附件按钮点击事件
    void onSearchHistory();                           // 处理历史记录搜索事件
    void toggleSidebar();                            // 切换左侧边栏的展开与收起状态
    void onSessionContextMenu(const QPoint& pos);   // 处理会话列表的右键菜单请求
private:
    void initializeContext();                                      // 初始化对话上下文和系统默认提示词
    void appendMessage(const QString& role, const QString& msg, bool saveToDb = true); // 将消息渲染并追加到界面
    void rebuildAdvancedUI();                                      // 重构复杂的左右分栏聊天界面
    bool handleLocalIntent(const QString& inputText);              // 拦截并处理本地特定意图（如查询今日考勤）
    QString parseMarkdown(const QString& md);                      // 将简单的Markdown文本转换为HTML格式以供显示
    void speakText(const QString& text);                           // 调用系统命令行接口将文本转换为语音播报
    void loadSessionsFromDB();                                     // 从服务端获取并加载用户的历史会话列表
    void loadChatHistoryFromDB(const QString& sessionId);          // 从服务端获取并加载指定会话的全部聊天记录
    void saveMessageToDB(const QString& sessionId, const QString& role, const QString& content); //将聊天消息异步保存到数据库
    // 服务端探针
    void sendAuditToServer(const QString& sessionId, const QString& role, const QString& content); // 聊天文本发送到服务端进行记录
    void sendAuditFileToServer(const QString& sessionId, const QString& fileName, const QByteArray& fileData); //上传文件数据发送到服务端进行拦截
    // 控件
    QTextBrowser* m_textBrowser;    // 聊天内容显示控件
    QLineEdit* m_oldLineEdit;       // 单行输入框（已被隐藏）
    QPushButton* m_sendBtn;         // 发送消息按钮
    QPushButton* m_clearBtn;        // 清空消息按钮
    QPushButton* m_voiceBtn;        //语音播报切换按钮
    QWidget* m_leftWidget;          // 左侧边栏容器
    QPushButton* m_toggleSidebarBtn;  // 侧边栏折叠/展开按钮
    QListWidget* m_sessionList;        // 历史会话列表控件
    QPushButton* m_newSessionBtn;     // 新建对话按钮
    QPushButton* m_attachBtn;         // 附件上传按钮
    QLineEdit* m_searchBox;            // 会话历史搜索框
    QTextEdit* m_inputTextEdit;        // 多行文本输入框
    QNetworkAccessManager* m_networkManager;   // 网络访问管理器，用于发起HTTP请求
    QString m_apiUrl;                        //当前使用的API接口地址
    QString m_apiKey;                        //大模型接口调用的鉴权密钥
    QString m_modelName;                     //当前选定的AI模型名称
    QJsonArray m_messageHistory;           // 当前会话的上下文消息数组，用于传递给大模型
    QString m_currentHtmlDisplay;          // 当前聊天窗口的完整HTML渲染代码
    QString m_userName;                  // 当前登录的用户名
    QString m_currentSessionId;          // 当前正在进行的会话唯一ID
    QString m_fileContext;                // 当前准备发送的附件内容解析文本
    QList<QPair<QString, QByteArray>> m_pendingFiles;     // 待上传附件的队列（文件名与二进制数据的键值对）
    bool m_isReplying;                               // 标记当前是否正在等待网络回复，防止重复点击
    bool m_voiceEnabled;                             //标记当前语音播报功能是否处于开启状态
};
#endif // AIASSISTANTMODULE_H
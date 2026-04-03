#ifndef CHATMODULE_H
#define CHATMODULE_H
#include <QObject>
#include <QTcpSocket>
#include <QTextBrowser>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QTextEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QUuid>
#include <QImage>
#include <QBuffer>
class ChatModule : public QObject
{
    Q_OBJECT
public:
    // 构造并绑定界面控件到通讯模块
    ChatModule(QListWidget* contactsList, QTextBrowser* textBrowser, QLineEdit* lineEdit, QLabel* targetLabel, QPushButton* btnEmoji, QPushButton* btnFolder, QPushButton* btnHistory, QPushButton* btnMoreOpt, QLineEdit* searchEdit, QObject* parent = nullptr); 
    void connectToServer(const QString& ip, quint16 port, const QString& myName); // 建立到服务器的 TCP 连接并登录
    void loadContactsFromDatabase();                                              // 从本地数据库加载联系人与群组列表
    void sendBroadcast(const QString& msg);                                       // 发送系统级广播消息到服务器
public slots:
    void sendMessage();                                                           // 发送消息（文本/图片/文件）到当前会话
    void onContactSwitched(int currentRow);                                       // 切换联系人时加载对应会话历史与 UI
    void sendSystemMessage(const QString& to, const QString& msg);                // 发送系统通知类的结构化消息

signals:
    void broadcastReceived(QString from, QString msg);                            // 接收到广播时发出信号以更新 UI
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;                       // 事件过滤用于拦截输入框或按钮的自定义行为
private slots:
    void onConnected();                                     // 套接字连接建立后的初始化处理
    void onDisconnected();                                 // 套接字断开时的清理逻辑
    void onReadyRead();                                    // 读取并解析来自服务器的消息数据
    void onBtnEmojiClicked();                              // 处理表情按钮点击展示表情面板
    void onBtnHistoryClicked();                            // 打开历史表情或常用项面板
    void onBtnFolderClicked();                             // 打开文件选择对话框并准备上传
    void onBtnMoreOptClicked();                            // 展示更多操作菜单（名片/群组信息）
private:
    // 界面控件指针
    QListWidget* m_contactsList;                           // 联系人列表视图指针
    QTextBrowser* m_textBrowser;                          // 聊天消息展示区域
    QLineEdit* m_lineEdit;                                // 消息输入框
    QLabel* m_targetLabel;                                 // 显示当前会话目标的标签
    QPushButton* m_btnEmoji;                               // 表情按钮
    QPushButton* m_btnFolder;                              // 文件/图片选择按钮
    QPushButton* m_btnHistory;                             // 历史表情按钮
    QPushButton* m_btnMoreOpt;                             // 更多选项按钮
    QLineEdit* m_searchEdit;                               // 联系人搜索输入框
    // 核心网络与状态变量
    QTcpSocket* m_tcpSocket;                               // 与服务器通信的 TCP 套接字
    QString m_myName;                                      // 本地登录用户名
    QString m_myEmpId;                                     // 本地员工工号（预留字段）
    QString m_currentTarget;                               // 当前会话目标名（用户或群组）
    bool m_isCurrentGroup;                                 // 当前目标是否为群组
    QStringList m_recentEmojis;                            // 最近使用表情的缓存列表
    QMap<QString, QString> m_chatHistories;                // 缓存每个会话的 HTML 聊天记录
    QTextEdit* m_textEdit;                                 // 可编辑的消息输入区域（富文本）
    void showUserInfo(const QString& userName);            // 弹出并显示指定用户信息界面
    QMap<QString, QString> m_fileNameMap;                  // 本地文件路径与远端文件名映射
    void sendFileChunked(const QString& filePath, const QString& fileName,const QString& receiver, bool isGroup, const QString& dept); // 分块发送大文件以支持断点续传
    void executeFileSend(const QString& filePath);         // 执行文件发送的具体流程
};
#endif // CHATMODULE_H
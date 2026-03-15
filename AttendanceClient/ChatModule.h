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
    // 初始化通讯模块并绑定界面控件
    ChatModule(QListWidget* contactsList, QTextBrowser* textBrowser, QLineEdit* lineEdit, QLabel* targetLabel, QPushButton* btnEmoji, QPushButton* btnFolder, QPushButton* btnHistory, QPushButton* btnMoreOpt, QLineEdit* searchEdit, QObject* parent = nullptr);
    void connectToServer(const QString& ip, quint16 port, const QString& myName); // 发起TCP网络连接以连接服务端
    void loadContactsFromDatabase();                                              // 从数据库检索并加载员工及群聊列表
    void sendBroadcast(const QString& msg);                                       // 触发并发送全局系统广播消息
public slots:
    void sendMessage();                                                           // 处理文本、图片或文件的消息外发逻辑
    void onContactSwitched(int currentRow);                                       // 响应左侧列表切换，重新加载对应聊天上下文
    void sendSystemMessage(const QString& to, const QString& msg);                // 向特定目标发送不可篡改的系统结构化信息

signals:
    void broadcastReceived(QString from, QString msg);                            // 接收到全局广播消息时向外触发该信号
private slots:
    void onConnected();                                     // 监听到Socket连接成功，发送初始化登录校验报文
    void onDisconnected();                                 // 监听到Socket断开，进行连接状态清理
    void onReadyRead();                                    // 异步读取并解析从网络套接字传来的JSON通讯数据
    void onBtnEmojiClicked();                              // 响应按钮操作：唤出Emoji表情选择面板
    void onBtnHistoryClicked();                            // 响应按钮操作：唤出历史常用表情快捷面板
    void onBtnFolderClicked();                             // 响应按钮操作：打开本地文件管理器选择附件
    void onBtnMoreOptClicked();                            // 响应按钮操作：弹出个人资料名片或群成员列表
private:
    // 控件
    QListWidget* m_contactsList;                           // 联系人列表显示控件
    QTextBrowser* m_textBrowser;                          // 富文本聊天内容展示大屏控件
    QLineEdit* m_lineEdit;                               // 底部的聊天消息文本输入框
    QLabel* m_targetLabel;                              // 顶部用于显示当前聊天目标名称的标签
    QPushButton* m_btnEmoji;                           // 调出表情包菜单的触发按钮
    QPushButton* m_btnFolder;                           // 选择本地文件或图片进行上传的触发按钮
    QPushButton* m_btnHistory;                        // 快捷浏览并复用历史操作的触发按钮
    QPushButton* m_btnMoreOpt;                        // 查看联系人名片或群组详情的触发按钮
    QLineEdit* m_searchEdit;                       // 左侧联系人列表上方的模糊搜索过滤输入框
    // 核心变量
    QTcpSocket* m_tcpSocket;                    // 底层TCP通讯套接字对象，负责二进制传输
    QString m_myName;                           // 记录当前成功登录系统的本地用户名称
    QString m_myEmpId;                          // 记录当前登录用户的本地唯一工号(预留)
    QString m_currentTarget;                   // 记录当前选中的对话对象名称（单聊或群聊名）
    bool m_isCurrentGroup;                     // 状态标识：当前对话是否处于部门群聊模式
    QStringList m_recentEmojis;               // LRU缓存队列：存储用户最近使用过的表情符号
    QMap<QString, QString> m_chatHistories;      // 内存字典：按照对话目标名称缓存的HTML完整记录
};
#endif // CHATMODULE_H
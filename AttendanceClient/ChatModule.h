#pragma once

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

class ChatModule : public QObject {
    Q_OBJECT
public:
    // 初始化通讯模块并绑定界面控件
    explicit ChatModule(QListWidget* contactsList, QTextBrowser* textBrowser,
        QLineEdit* lineEdit, QLabel* targetLabel,
        QPushButton* btnEmoji, QPushButton* btnFolder,
        QPushButton* btnHistory, QPushButton* btnMoreOpt,
        QLineEdit* searchEdit,
        QObject* parent = nullptr);                                

    void connectToServer(const QString& ip, quint16 port, const QString& myName); // 发起TCP连接
    void loadContactsFromDatabase();                               // 从数据库检索并加载员工及群聊列表
    void sendBroadcast(const QString& msg);                        // 发送全局广播消息

public slots:
    void sendMessage();                                            // 处理文本、图片或文件的外发逻辑
    void onContactSwitched(int currentRow);                        // 切换左侧列表时的对话上下文处理
    void sendSystemMessage(const QString& to, const QString& msg); // 在聊天窗口显示系统级别的提示信息

signals:
    void broadcastReceived(QString fromUser, QString msg);         // 接收到广播消息的信号

private slots:
    void onConnected();                                            // 网络连接成功的回调处理
    void onDisconnected();                                         // 网络连接断开的回调处理
    void onReadyRead();                                            // 解析服务器下发的JSON协议报文
    void onBtnEmojiClicked();                                      // 表情按钮点击处理
    void onBtnHistoryClicked();                                    // 历史表情记录点击处理
    void onBtnFolderClicked();                                     // 附件或图片选择按钮处理
    void onBtnMoreOptClicked();                                    // 更多选项菜单处理

private:
    QListWidget* m_contactsList;                                   // 联系人列表显示控件
    QTextBrowser* m_textBrowser;                                   // 聊天内容展示控件
    QLineEdit* m_lineEdit;                                         // 消息文本输入框
    QLabel* m_targetLabel;                                         // 当前聊天目标名称显示标签

    QPushButton* m_btnEmoji;                                       // 调出表情包菜单按钮
    QPushButton* m_btnFolder;                                      // 选择本地文件上传按钮
    QPushButton* m_btnHistory;                                     // 历史记录查看按钮
    QPushButton* m_btnMoreOpt;                                     // 更多操作按钮（如查看名片/群成员）
    QLineEdit* m_searchEdit;                                       // 联系人搜索过滤输入框

    QTcpSocket* m_tcpSocket;                                       // 底层TCP通讯套接字

    QString m_myName;                                              // 当前登录的本地用户名
    QString m_myEmpId;                                             // 当前登录的本地用户工号
    QString m_currentTarget;                                       // 当前对话的目标对象名称
    bool m_isCurrentGroup;                                         // 标识当前对话是否为群聊模式

    QStringList m_recentEmojis;                                    // 最近使用的表情缓存列表
    QMap<QString, QString> m_chatHistories;                        // 按照目标名称缓存的HTML格式聊天记录
};
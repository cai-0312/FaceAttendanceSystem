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
#include <QUuid>   // 用于生成消息唯一ID，实现已读回执功能的关键
#include <QImage>  // 用于处理即时通讯中的图片数据与渲染
#include <QBuffer> // 用于将二进制图片转换为Base64字符串进行网络传输

class ChatModule : public QObject {
    Q_OBJECT
public:
    explicit ChatModule(QListWidget* contactsList, QTextBrowser* textBrowser,
        QLineEdit* lineEdit, QLabel* targetLabel,
        QPushButton* btnEmoji, QPushButton* btnFolder,
        QPushButton* btnHistory, QPushButton* btnMoreOpt,
        QLineEdit* searchEdit,
        QObject* parent = nullptr);

    // 发起TCP连接：指定服务器IP、端口及本地登录用户名
    void connectToServer(const QString& ip, quint16 port, const QString& myName);
    // 从数据库中检索并加载员工列表及部门群聊列表
    void loadContactsFromDatabase();
    void sendBroadcast(const QString& msg);

public slots:
    // 发送消息槽函数：处理文本、图片或文件的外发逻辑
    void sendMessage();
    // 切换联系人槽函数：当用户点击左侧列表时切换对话上下文
    void onContactSwitched(int currentRow);
    // 发送系统通知：用于在聊天窗口显示系统级别的提示信息
    void sendSystemMessage(const QString& to, const QString& msg);

signals:
    void broadcastReceived(QString fromUser, QString msg);

private slots:
    // 网络连接成功的回调处理
    void onConnected();
    // 网络连接断开的回调处理
    void onDisconnected();
    // 接收数据回调：解析服务器下发的JSON协议报文
    void onReadyRead();
    // 表情按钮点击处理
    void onBtnEmojiClicked();
    // 历史表情记录点击处理
    void onBtnHistoryClicked();
    // 附件/图片选择按钮处理
    void onBtnFolderClicked();
    // 更多选项菜单处理
    void onBtnMoreOptClicked();

private:
    // 界面控件成员指针
    QListWidget* m_contactsList;
    QTextBrowser* m_textBrowser;
    QLineEdit* m_lineEdit;
    QLabel* m_targetLabel;

    QPushButton* m_btnEmoji;
    QPushButton* m_btnFolder;
    QPushButton* m_btnHistory;
    QPushButton* m_btnMoreOpt;
    QLineEdit* m_searchEdit;

    // TCP通讯套接字
    QTcpSocket* m_tcpSocket;

    // 用户身份与当前会话状态信息
    QString m_myName;
    QString m_myEmpId;
    QString m_currentTarget; // 当前对话的目标名称（人员姓名或部门名称）
    bool m_isCurrentGroup;   // 状态标识：当前是否处于群聊模式

    // 缓存数据结构
    QStringList m_recentEmojis;
    QMap<QString, QString> m_chatHistories; // 按照目标名称缓存HTML格式的聊天记录
};
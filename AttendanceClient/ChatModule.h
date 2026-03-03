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

// 引入 JSON 与文件处理库
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QCoreApplication>

class ChatModule : public QObject {
    Q_OBJECT
public:
    // ★ 构造函数升级：接收表情、文件、历史、更多选项这4个新按钮
    explicit ChatModule(QListWidget* contactsList, QTextBrowser* textBrowser,
        QLineEdit* lineEdit, QLabel* targetLabel,
        QPushButton* btnEmoji, QPushButton* btnFolder,
        QPushButton* btnHistory, QPushButton* btnMoreOpt,
        QLineEdit* searchEdit, // ★ 新增搜索框指针
        QObject* parent = nullptr);

    void connectToServer(const QString& ip, quint16 port, const QString& myName);
    void loadContactsFromDatabase();

public slots:
    void sendMessage();
    void onContactSwitched(int currentRow);
    void sendSystemMessage(const QString& to, const QString& msg);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();

    // ★ 新增功能槽函数
    void onBtnEmojiClicked();   // 发送表情
    void onBtnHistoryClicked(); // 历史表情
    void onBtnFolderClicked();  // 发送文件
    void onBtnMoreOptClicked(); // 查看个人信息

private:
    // UI 指针
    QListWidget* m_contactsList;
    QTextBrowser* m_textBrowser;
    QLineEdit* m_lineEdit;
    QLabel* m_targetLabel;

    QPushButton* m_btnEmoji;
    QPushButton* m_btnFolder;
    QPushButton* m_btnHistory;
    QPushButton* m_btnMoreOpt;

    QTcpSocket* m_tcpSocket;

    QString m_myName;
    QString m_myEmpId;       // ★ 我的工号 (用于建文件夹)
    QString m_currentTarget;

    QStringList m_recentEmojis; // ★ 保存最近使用的表情包

    QMap<QString, QString> m_chatHistories;

    QLineEdit* m_searchEdit;
};
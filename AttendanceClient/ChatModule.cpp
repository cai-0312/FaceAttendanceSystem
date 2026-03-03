#include "ChatModule.h"
#include <QDateTime>
#include <QMessageBox>
#include <QTimer> 
#include <QSqlQuery>
#include <QSqlError>
#include <QFileDialog>
#include <QFileInfo>
#include <QAction>
#include <QRegularExpression> // 确保Qt6正则表达式正常工作

ChatModule::ChatModule(QListWidget* contactsList, QTextBrowser* textBrowser,
    QLineEdit* lineEdit, QLabel* targetLabel,
    QPushButton* btnEmoji, QPushButton* btnFolder,
    QPushButton* btnHistory, QPushButton* btnMoreOpt,
    QLineEdit* searchEdit,
    QObject* parent)
    : QObject(parent), m_contactsList(contactsList), m_textBrowser(textBrowser),
    m_lineEdit(lineEdit), m_targetLabel(targetLabel),
    m_btnEmoji(btnEmoji), m_btnFolder(btnFolder), m_btnHistory(btnHistory), m_btnMoreOpt(btnMoreOpt),
    m_searchEdit(searchEdit)
{
    m_tcpSocket = new QTcpSocket(this);

    connect(m_tcpSocket, &QTcpSocket::connected, this, &ChatModule::onConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ChatModule::onDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ChatModule::onReadyRead);
    connect(m_contactsList, &QListWidget::currentRowChanged, this, &ChatModule::onContactSwitched);

    // 绑定四个新功能按钮
    if (m_btnEmoji) connect(m_btnEmoji, &QPushButton::clicked, this, &ChatModule::onBtnEmojiClicked);
    if (m_btnFolder) connect(m_btnFolder, &QPushButton::clicked, this, &ChatModule::onBtnFolderClicked);
    if (m_btnHistory) connect(m_btnHistory, &QPushButton::clicked, this, &ChatModule::onBtnHistoryClicked);
    if (m_btnMoreOpt) connect(m_btnMoreOpt, &QPushButton::clicked, this, &ChatModule::onBtnMoreOptClicked);

    m_targetLabel->setText("请在左侧选择联系人开始聊天");

    // ★ 双搜引擎：搜索联系人 + 搜索聊天记录
    if (m_searchEdit) {
        connect(m_searchEdit, &QLineEdit::textChanged, this, [=](const QString& text) {
            for (int i = 0; i < m_contactsList->count(); ++i) {
                QListWidgetItem* item = m_contactsList->item(i);
                item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
            }
            });

        connect(m_searchEdit, &QLineEdit::returnPressed, this, [=]() {
            QString keyword = m_searchEdit->text();
            if (keyword.isEmpty()) return;
            if (!m_textBrowser->find(keyword)) {
                m_textBrowser->moveCursor(QTextCursor::Start);
                if (!m_textBrowser->find(keyword)) {
                    QMessageBox::information(nullptr, "未找到", "当前聊天记录中未找到相关内容。");
                }
            }
            });
    }
}

void ChatModule::connectToServer(const QString& ip, quint16 port, const QString& myName) {
    m_myName = myName;
    loadContactsFromDatabase();
    m_tcpSocket->connectToHost(ip, port);
}

// 🚀 1. 加载联系人 (001格式工号 + 隐藏超级管理员)
void ChatModule::loadContactsFromDatabase() {
    m_contactsList->clear();

    QSqlQuery myQuery;
    myQuery.prepare("SELECT id FROM users WHERE name = :n");
    myQuery.bindValue(":n", m_myName);
    if (myQuery.exec() && myQuery.next()) {
        m_myEmpId = myQuery.value(0).toString();
    }

    QSqlQuery query("SELECT id, name, department, role FROM users");
    while (query.next()) {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString().trimmed();
        QString dept = query.value(2).toString().trimmed();
        QString role = query.value(3).toString().trimmed();

        if (name == m_myName) {
            m_myEmpId = QString::number(id);
            continue;
        }

        // ★ 核心拦截：只要他的角色叫"超级管理员"（或者名字叫admin），直接跳过不显示！
        if (role.contains("超级管理") || role.contains("admin") || name.toLower().contains("admin")) {
            continue;
        }

        // 格式化工号为 3 位数 (例如 001)
        QString formattedId = QString("%1").arg(id, 3, 10, QChar('0'));
        QString icon = (role == "管理员") ? "👨‍💼" : "👨‍💻";

        QListWidgetItem* item = new QListWidgetItem(QString("%1 %2 [%3] (%4)").arg(icon, name, formattedId, dept));
        item->setData(Qt::UserRole, name);
        m_contactsList->addItem(item);
    }
}

void ChatModule::onContactSwitched(int currentRow) {
    if (currentRow < 0) return;
    m_currentTarget = m_contactsList->item(currentRow)->data(Qt::UserRole).toString();
    m_targetLabel->setText("正在与 【" + m_currentTarget + "】 聊天");
    m_textBrowser->setHtml(m_chatHistories.value(m_currentTarget, ""));
    m_textBrowser->moveCursor(QTextCursor::End);
}

// 🚀 2. 发送文本消息
void ChatModule::sendMessage() {
    QString msg = m_lineEdit->text().trimmed();
    if (msg.isEmpty() || m_currentTarget.isEmpty()) return;

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString myMsgHtml = "<div style='text-align:right; margin-bottom:10px;'>"
        "<span style='color:#999999; font-size:12px;'>" + timeStr + " [我] </span><br>"
        "<span style='background-color:#95EC69; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#000000;'>"
        + msg + "</span></div>";

    m_chatHistories[m_currentTarget] += myMsgHtml;
    m_textBrowser->setHtml(m_chatHistories[m_currentTarget]);
    m_textBrowser->moveCursor(QTextCursor::End);
    m_lineEdit->clear();

    if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject json; json["type"] = "chat"; json["from"] = m_myName; json["to"] = m_currentTarget; json["msg"] = msg;
        m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
    }
}

// 🚀 3. 发送文件
void ChatModule::onBtnFolderClicked() {
    if (m_currentTarget.isEmpty()) {
        QMessageBox::warning(nullptr, "提示", "请先选择聊天对象！"); return;
    }

    QString filePath = QFileDialog::getOpenFileName(nullptr, "选择要发送的文件", "", "所有文件 (*.*)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (file.size() > 10 * 1024 * 1024) {
        QMessageBox::warning(nullptr, "超限", "为了保证网络流畅，目前仅支持发送 10MB 以内的文件！"); return;
    }

    if (file.open(QIODevice::ReadOnly)) {
        QByteArray fileData = file.readAll();
        QString base64Data = QString(fileData.toBase64());
        QFileInfo fi(filePath);
        QString fileName = fi.fileName();

        QString localDirPath = QCoreApplication::applicationDirPath() + "/client/" + m_myEmpId;
        QDir().mkpath(localDirPath);
        file.copy(localDirPath + "/" + fileName);
        file.close();

        if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject json;
            json["type"] = "file";
            json["from"] = m_myName;
            json["to"] = m_currentTarget;
            json["filename"] = fileName;
            json["filedata"] = base64Data;
            m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");

            sendSystemMessage(m_currentTarget, "📁 成功发送文件: " + fileName);
        }
    }
}

// 🚀 4. 发送表情包
void ChatModule::onBtnEmojiClicked() {
    QMenu emojiMenu;
    QStringList emojis = { "😀", "😂", "😅", "😊", "😍", "😒", "😭", "😡", "👍", "🙏", "🎉", "🔥", "💔", "🤝" };
    for (const QString& em : emojis) {
        QAction* act = emojiMenu.addAction(em);
        connect(act, &QAction::triggered, this, [=]() {
            m_lineEdit->insert(em);
            m_recentEmojis.removeAll(em);
            m_recentEmojis.prepend(em);
            if (m_recentEmojis.size() > 5) m_recentEmojis.removeLast();
            });
    }
    emojiMenu.setStyleSheet("QMenu { font-size: 20px; }");
    emojiMenu.exec(QCursor::pos());
}

// 🚀 5. 历史表情包
void ChatModule::onBtnHistoryClicked() {
    if (m_recentEmojis.isEmpty()) {
        QMessageBox::information(nullptr, "提示", "暂无历史表情记录。"); return;
    }
    QMenu historyMenu;
    for (const QString& em : m_recentEmojis) {
        QAction* act = historyMenu.addAction(em);
        connect(act, &QAction::triggered, this, [=]() { m_lineEdit->insert(em); });
    }
    historyMenu.setStyleSheet("QMenu { font-size: 20px; }");
    historyMenu.exec(QCursor::pos());
}

// 🚀 6. 个人名片资料
void ChatModule::onBtnMoreOptClicked() {
    QString targetName = m_currentTarget.isEmpty() ? m_myName : m_currentTarget;

    QSqlQuery query;
    query.prepare("SELECT id, department, role FROM users WHERE name = :n");
    query.bindValue(":n", targetName);

    if (query.exec() && query.next()) {
        int rawId = query.value(0).toInt();
        QString formattedId = QString("%1").arg(rawId, 3, 10, QChar('0'));
        QString dept = query.value(1).toString();
        QString role = query.value(2).toString();

        QString info = QString("👤 姓名：%1\n🆔 工号：%2\n🏢 部门：%3\n🔑 身份：%4")
            .arg(targetName, formattedId, dept, role);
        QMessageBox::information(nullptr, targetName + " 的企业资料", info);
    }
}

// 🚀 7. 网络接收与解析
void ChatModule::onReadyRead() {
    while (m_tcpSocket->canReadLine()) {
        QByteArray data = m_tcpSocket->readLine();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) continue;

        QJsonObject json = doc.object();
        QString type = json["type"].toString();
        QString fromUser = json["from"].toString();

        if (type == "chat") {
            QString content = json["msg"].toString();
            QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
            QString receiveHtml = "<div style='text-align:left; margin-bottom:10px;'>"
                "<span style='color:#999999; font-size:12px;'> [" + fromUser + "] " + timeStr + "</span><br>"
                "<span style='background-color:#FFFFFF; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; border:1px solid #EAEAEA; color:#000000;'>"
                + content + "</span></div>";

            m_chatHistories[fromUser] += receiveHtml;
        }
        else if (type == "file") {
            QString fileName = json["filename"].toString();
            QByteArray fileData = QByteArray::fromBase64(json["filedata"].toString().toUtf8());

            QString localDirPath = QCoreApplication::applicationDirPath() + "/client/" + m_myEmpId;
            QDir().mkpath(localDirPath);
            QFile file(localDirPath + "/" + fileName);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(fileData);
                file.close();
            }

            QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
            QString receiveHtml = "<div style='text-align:left; margin-bottom:10px;'>"
                "<span style='color:#999999; font-size:12px;'> [" + fromUser + "] " + timeStr + "</span><br>"
                "<span style='background-color:#E6A23C; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#FFFFFF;'>"
                "📁 收到文件: " + fileName + "<br>(已自动保存在您的工号文件夹下)</span></div>";
            m_chatHistories[fromUser] += receiveHtml;
        }

        if (m_currentTarget == fromUser) {
            m_textBrowser->setHtml(m_chatHistories[fromUser]);
            m_textBrowser->moveCursor(QTextCursor::End);
        }
    }
}

// 辅助方法
void ChatModule::sendSystemMessage(const QString& to, const QString& msg) {
    if (to.isEmpty() || msg.isEmpty()) return;
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString myMsgHtml = "<div style='text-align:right; margin-bottom:10px;'><span style='color:#999999; font-size:12px;'>" + timeStr + " [系统代发] </span><br><span style='background-color:#95EC69; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#000000;'>" + msg + "</span></div>";
    m_chatHistories[to] += myMsgHtml;
    if (m_currentTarget == to) { m_textBrowser->setHtml(m_chatHistories[to]); m_textBrowser->moveCursor(QTextCursor::End); }
    if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject json; json["type"] = "chat"; json["from"] = m_myName; json["to"] = to; json["msg"] = msg;
        m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
    }
}

void ChatModule::onConnected() {
    QJsonObject json; json["type"] = "login"; json["name"] = m_myName;
    m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
}
void ChatModule::onDisconnected() { m_targetLabel->setText("网络已断开"); }
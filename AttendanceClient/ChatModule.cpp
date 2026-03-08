#include "ChatModule.h"
#include <QDateTime>
#include <QMessageBox>
#include <QTimer> 
#include <QFileDialog>
#include <QFileInfo>
#include <QAction>
#include <QScrollBar>
#include <QDesktopServices> 
#include <QUrl>
#include <QDir>
#include <QDialog>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication> 
#include <QTcpSocket>

// 🚀 核心通讯组件：通过短连接获取联系人列表和用户资料
static QJsonObject requestDataFromServer(const QJsonObject& jsonRequest) {
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", 9999);
    QJsonObject responseJson;
    if (socket.waitForConnected(2000)) {
        QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
        socket.write(block);
        socket.waitForBytesWritten(1000);
        if (socket.waitForReadyRead(5000)) {
            QByteArray responseData;
            while (socket.waitForReadyRead(50) || socket.bytesAvailable() > 0) {
                responseData += socket.readAll();
                if (responseData.endsWith("\n")) break;
            }
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (!doc.isNull()) responseJson = doc.object();
        }
        socket.disconnectFromHost();
    }
    return responseJson;
}

ChatModule::ChatModule(QListWidget* contactsList, QTextBrowser* textBrowser,
    QLineEdit* lineEdit, QLabel* targetLabel,
    QPushButton* btnEmoji, QPushButton* btnFolder,
    QPushButton* btnHistory, QPushButton* btnMoreOpt,
    QLineEdit* searchEdit, QObject* parent)
    : QObject(parent), m_contactsList(contactsList), m_textBrowser(textBrowser),
    m_lineEdit(lineEdit), m_targetLabel(targetLabel),
    m_btnEmoji(btnEmoji), m_btnFolder(btnFolder), m_btnHistory(btnHistory), m_btnMoreOpt(btnMoreOpt),
    m_searchEdit(searchEdit), m_isCurrentGroup(false)
{
    m_tcpSocket = new QTcpSocket(this);

    m_textBrowser->setOpenLinks(false);
    m_textBrowser->setOpenExternalLinks(false);

    connect(m_textBrowser, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        QDesktopServices::openUrl(url);
        });

    connect(m_tcpSocket, &QTcpSocket::connected, this, &ChatModule::onConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ChatModule::onDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ChatModule::onReadyRead);

    connect(m_contactsList, &QListWidget::currentRowChanged, this, &ChatModule::onContactSwitched);

    if (m_btnEmoji) connect(m_btnEmoji, &QPushButton::clicked, this, &ChatModule::onBtnEmojiClicked);
    if (m_btnFolder) connect(m_btnFolder, &QPushButton::clicked, this, &ChatModule::onBtnFolderClicked);
    if (m_btnHistory) connect(m_btnHistory, &QPushButton::clicked, this, &ChatModule::onBtnHistoryClicked);
    if (m_btnMoreOpt) connect(m_btnMoreOpt, &QPushButton::clicked, this, &ChatModule::onBtnMoreOptClicked);

    m_targetLabel->setText("请在左侧选择联系人开始聊天");
}

void ChatModule::connectToServer(const QString& ip, quint16 port, const QString& myName) {
    m_myName = myName;
    loadContactsFromDatabase();
    m_tcpSocket->connectToHost(ip, port);
}

// 🚀 核心改造 1：向服务器拉取联系人列表
void ChatModule::loadContactsFromDatabase() {
    m_contactsList->clear();

    QJsonObject req;
    req["type"] = "query_chat_contacts";
    req["name"] = m_myName;
    QJsonObject res = requestDataFromServer(req);

    if (res["status"].toString() != "success") return;

    QString myDept = res["my_dept"].toString();
    QString myLocalFolder = res["my_folder"].toString();
    this->setProperty("localFolder", myLocalFolder);

    QListWidgetItem* allGroupItem = new QListWidgetItem("📢 【公司总群】");
    allGroupItem->setData(Qt::UserRole, "GROUP_公司总群");
    m_contactsList->addItem(allGroupItem);

    QJsonArray deptArr = res["departments"].toArray();
    for (int i = 0; i < deptArr.size(); ++i) {
        QString dept = deptArr[i].toString();
        QListWidgetItem* item = new QListWidgetItem(QString("👨‍👩‍👧‍👦 【部门群】%1").arg(dept));
        item->setData(Qt::UserRole, "GROUP_" + dept);
        m_contactsList->addItem(item);
    }

    QJsonArray userArr = res["users"].toArray();
    for (int i = 0; i < userArr.size(); ++i) {
        QJsonObject u = userArr[i].toObject();
        QString name = u["name"].toString();
        QString dept = u["department"].toString();
        QString role = u["role"].toString();
        QString formattedId = QString("%1").arg(u["id"].toInt(), 3, 10, QChar('0'));
        QString icon = (role == "管理员") ? "👨‍💼" : "👨‍💻";

        QListWidgetItem* item = new QListWidgetItem(QString("%1 %2 [%3] (%4)").arg(icon, name, formattedId, dept));
        item->setData(Qt::UserRole, name);
        m_contactsList->addItem(item);
    }
}

void ChatModule::onContactSwitched(int currentRow) {
    if (currentRow < 0) return;
    QString targetData = m_contactsList->item(currentRow)->data(Qt::UserRole).toString();

    if (targetData.startsWith("GROUP_")) {
        m_isCurrentGroup = true;
        m_currentTarget = targetData.replace("GROUP_", "");
        m_targetLabel->setText("正在与 【群聊：" + m_currentTarget + "】 聊天");
    }
    else {
        m_isCurrentGroup = false;
        m_currentTarget = targetData;
        m_targetLabel->setText("正在与 【" + m_currentTarget + "】 聊天");

        if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject json;
            json["type"] = "read_receipt";
            json["from"] = m_myName;
            json["to"] = m_currentTarget;
            m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
        }
    }
    m_textBrowser->setHtml(m_chatHistories.value(m_currentTarget, ""));
    m_textBrowser->moveCursor(QTextCursor::End);
}

void ChatModule::sendMessage() {
    QString msg = m_lineEdit->text().trimmed();
    if (msg.isEmpty() || m_currentTarget.isEmpty()) return;

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString readStatus = m_isCurrentGroup ? "" : QString("<span style='color:#AAAAAA; font-size:12px;'> (未读)</span>");

    QString myMsgHtml = "<div style='text-align:right; margin-bottom:10px;'>"
        "<span style='color:#999999; font-size:12px;'>" + timeStr + " [我] </span>" + readStatus + "<br>"
        "<span style='background-color:#95EC69; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#000000;'>"
        + msg + "</span></div>";

    m_chatHistories[m_currentTarget] += myMsgHtml;
    m_textBrowser->setHtml(m_chatHistories[m_currentTarget]);
    m_textBrowser->moveCursor(QTextCursor::End);
    m_lineEdit->clear();

    if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject json;
        json["from"] = m_myName;
        json["msg"] = msg;
        json["msg_id"] = msgId;

        if (m_isCurrentGroup) {
            json["type"] = "group_chat";
            json["department"] = m_currentTarget;
        }
        else {
            json["type"] = "chat";
            json["to"] = m_currentTarget;
        }
        m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
    }
}

void ChatModule::onBtnFolderClicked() {
    if (m_currentTarget.isEmpty()) {
        QMessageBox::warning(nullptr, "提示", "请选择聊天对象！");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(nullptr, "发送文件", "", "所有文件 (*.*)");
    if (filePath.isEmpty()) return;

    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();
    bool isImage = (suffix == "png" || suffix == "jpg" || suffix == "jpeg");

    QFile file(filePath);
    if (file.size() > 5 * 1024 * 1024) {
        QMessageBox::warning(nullptr, "超限", "限制发送5MB内文件！");
        return;
    }

    QString myLocalFolder = this->property("localFolder").toString();
    if (myLocalFolder.isEmpty()) myLocalFolder = "Unknown_User";

    QString clientDirPath = QCoreApplication::applicationDirPath() + "/ChatFiles/client/" + myLocalFolder;
    QDir dir(clientDirPath);
    if (!dir.exists()) dir.mkpath(".");

    QString localFileName = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + fi.fileName();
    QString copiedFilePath = clientDirPath + "/" + localFileName;
    QFile::copy(filePath, copiedFilePath);

    if (file.open(QIODevice::ReadOnly)) {
        QByteArray fileData;
        if (isImage) {
            QImage img(filePath);
            img = img.scaled(400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QBuffer buffer(&fileData);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, suffix.toUpper().toLatin1());
        }
        else {
            fileData = file.readAll();
        }

        QString base64Data = QString(fileData.toBase64());
        QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
        QString displayContent;
        QString fileUrl = QUrl::fromLocalFile(copiedFilePath).toString();

        if (isImage) {
            displayContent = QString("<a href='%1'><img src='data:image/%2;base64,%3' width='150' style='border-radius:6px;' /></a><br><a href='%1' style='font-size:12px;color:gray;text-decoration:none;'>(点击外部查看原图)</a>").arg(fileUrl, suffix, base64Data);
        }
        else {
            displayContent = QString("<a href='%1' style='text-decoration:none; color:#3370FF;'>📁 已发送文件: %2<br><span style='font-size:12px;'>(点击使用系统软件打开)</span></a>").arg(fileUrl, fi.fileName());
        }

        QString myMsgHtml = "<div style='text-align:right; margin-bottom:10px;'>"
            "<span style='color:#999999; font-size:12px;'>" + timeStr + " [我] </span><br>"
            "<span style='background-color:#95EC69; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px;'>"
            + displayContent + "</span></div>";

        m_chatHistories[m_currentTarget] += myMsgHtml;
        m_textBrowser->setHtml(m_chatHistories[m_currentTarget]);
        m_textBrowser->moveCursor(QTextCursor::End);

        if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject json;
            json["from"] = m_myName;
            json["msg"] = base64Data;
            json["filename"] = fi.fileName();

            if (m_isCurrentGroup) {
                json["type"] = isImage ? "group_image" : "group_file";
                json["department"] = m_currentTarget;
            }
            else {
                json["type"] = isImage ? "image" : "file";
                json["to"] = m_currentTarget;
            }
            m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
        }
    }
}

void ChatModule::onReadyRead() {
    while (m_tcpSocket->canReadLine()) {
        QByteArray data = m_tcpSocket->readLine();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) continue;

        QJsonObject json = doc.object();
        QString type = json["type"].toString();
        QString fromUser = json["from"].toString();

        if (type == "broadcast") {
            QString content = json["msg"].toString();
            emit broadcastReceived(fromUser, content);

            QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
            QString receiveHtml = "<div style='text-align:center; margin-bottom:10px;'>"
                "<span style='background-color:#F56C6C; color:#FFF; padding:6px 12px; border-radius:6px; font-size:12px;'>"
                "📢 <b>全局系统广播</b> [" + fromUser + "] " + timeStr + "<br><br>" + content + "</span></div>";

            m_chatHistories["公司总群"] += receiveHtml;
            if (m_currentTarget == "公司总群") {
                m_textBrowser->setHtml(m_chatHistories["公司总群"]);
                m_textBrowser->moveCursor(QTextCursor::End);
            }
            continue;
        }

        if (type == "read_receipt") {
            m_chatHistories[fromUser].replace("color:#AAAAAA; font-size:12px;'> (未读)</span>", "color:#409EFF; font-size:12px;'> (已读)</span>");
            if (m_currentTarget == fromUser && !m_isCurrentGroup) {
                m_textBrowser->setHtml(m_chatHistories[fromUser]);
            }
            continue;
        }

        QString targetTab = type.contains("group") ? json["department"].toString() : fromUser;
        QString content = json["msg"].toString();
        QString timeStr = json.contains("time") ? json["time"].toString() : QDateTime::currentDateTime().toString("HH:mm:ss");
        QString offlineTag = json["is_offline"].toBool() ? "<span style='color:#F56C6C;'>(离线转入)</span>" : "";

        QString htmlContent;
        if (type == "chat" || type == "group_chat") {
            htmlContent = content;
        }
        else if (type == "image" || type == "group_image" || type == "file" || type == "group_file") {
            QString originalFileName = json["filename"].toString();
            QString suffix = originalFileName.split(".").last().toLower();
            if (suffix.isEmpty()) suffix = "png";

            QString myLocalFolder = this->property("localFolder").toString();
            if (myLocalFolder.isEmpty()) myLocalFolder = "Unknown_User";
            QString clientDirPath = QCoreApplication::applicationDirPath() + "/ChatFiles/client/" + myLocalFolder;
            QDir dir(clientDirPath);
            if (!dir.exists()) dir.mkpath(".");

            QString localFileName = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + originalFileName;
            QString localFilePath = clientDirPath + "/" + localFileName;

            QFile localFile(localFilePath);
            if (localFile.open(QIODevice::WriteOnly)) {
                localFile.write(QByteArray::fromBase64(content.toUtf8()));
                localFile.close();
            }

            QString fileUrl = QUrl::fromLocalFile(localFilePath).toString();

            if (type.contains("image")) {
                htmlContent = QString("<a href='%1'><img src='data:image/%2;base64,%3' width='150' style='border-radius:6px;' /></a><br><a href='%1' style='font-size:12px;color:gray;text-decoration:none;'>(点击外部查看原图)</a>").arg(fileUrl, suffix, content);
            }
            else {
                htmlContent = QString("<a href='%1' style='text-decoration:none; color:#3370FF;'>📁 收到文件: %2<br><span style='font-size:12px;'>(点击使用系统软件打开)</span></a>").arg(fileUrl, originalFileName);
            }
        }

        QString receiveHtml = "<div style='text-align:left; margin-bottom:10px;'>"
            "<span style='color:#999999; font-size:12px;'> [" + fromUser + "] " + timeStr + " " + offlineTag + "</span><br>"
            "<span style='background-color:#FFFFFF; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; border:1px solid #EAEAEA; color:#000000;'>"
            + htmlContent + "</span></div>";

        m_chatHistories[targetTab] += receiveHtml;

        if (m_currentTarget == targetTab) {
            m_textBrowser->setHtml(m_chatHistories[targetTab]);
            m_textBrowser->moveCursor(QTextCursor::End);

            if (type == "chat" || type == "image") {
                QJsonObject ack;
                ack["type"] = "read_receipt";
                ack["from"] = m_myName;
                ack["to"] = fromUser;
                m_tcpSocket->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + "\n");
            }
        }
    }
}

void ChatModule::onBtnEmojiClicked() {
    QMenu emojiMenu;
    QStringList emojis = { "😀", "😂", "😶", "😊", "😍", "😭", "😡", "👍", "🙏", "🎉" };
    for (const auto& em : emojis) {
        QAction* act = emojiMenu.addAction(em);
        connect(act, &QAction::triggered, this, [=]() { m_lineEdit->insert(em); });
    }
    emojiMenu.setStyleSheet("QMenu { font-size: 20px; }");
    emojiMenu.exec(QCursor::pos());
}

void ChatModule::onBtnHistoryClicked() { QMessageBox::information(nullptr, "提示", "历史表情面板。"); }

// 🚀 核心改造 2：向服务器拉取群成员名单或个人名片资料
void ChatModule::onBtnMoreOptClicked() {
    if (m_currentTarget.isEmpty()) {
        QMessageBox::warning(nullptr, "提示", "请先选择聊天对象！");
        return;
    }

    QDialog dialog(nullptr);
    dialog.setMinimumSize(320, 450);

    if (m_isCurrentGroup) {
        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QListWidget* listWidget = new QListWidget(&dialog);
        listWidget->setStyleSheet("QListWidget::item { height: 40px; font-size: 14px; border-bottom: 1px solid #F0F0F0; }");
        layout->addWidget(listWidget);

        QJsonObject req;
        req["type"] = "query_group_members";
        req["department"] = m_currentTarget;
        QJsonObject res = requestDataFromServer(req);

        int count = 0;
        if (res["status"].toString() == "success") {
            QJsonArray arr = res["data"].toArray();
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject u = arr[i].toObject();
                listWidget->addItem(QString("👤 %1 (%2 - %3)").arg(u["name"].toString(), u["dept"].toString(), u["job"].toString()));
                count++;
            }
        }
        dialog.setWindowTitle(QString("群成员列表 - %1 (%2人)").arg(m_currentTarget).arg(count));

        QPushButton* closeBtn = new QPushButton("关闭", &dialog);
        closeBtn->setMinimumHeight(35);
        layout->addWidget(closeBtn);
        connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    }
    else {
        dialog.setWindowTitle("个人资料名片");
        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        layout->setSpacing(20);
        layout->setContentsMargins(30, 30, 30, 30);

        QLabel* avatarLabel = new QLabel(&dialog);
        avatarLabel->setAlignment(Qt::AlignCenter);
        avatarLabel->setFixedSize(100, 100);
        avatarLabel->setStyleSheet("background-color: #EAEAEA; border-radius: 50px;");

        QFormLayout* form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignRight);
        form->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
        form->setHorizontalSpacing(20);
        form->setVerticalSpacing(15);

        QJsonObject req;
        req["type"] = "query_user_profile"; // 复用之前写好的接口
        req["name"] = m_currentTarget;
        QJsonObject res = requestDataFromServer(req);

        if (res["status"].toString() == "success") {
            QString d = res["department"].toString();
            QString j = res["job_title"].toString();
            QString g = res["gender"].toString();
            QString p = res["phone"].toString();
            QString avatarBase64 = res["avatar_base64"].toString();

            if (!avatarBase64.isEmpty()) {
                QImage img;
                img.loadFromData(QByteArray::fromBase64(avatarBase64.toUtf8()));
                QImage scaledImg = img.scaled(100, 100, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                QImage result(100, 100, QImage::Format_ARGB32_Premultiplied);
                result.fill(Qt::transparent);
                QPainter painter(&result);
                painter.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                path.addEllipse(0, 0, 100, 100);
                painter.setClipPath(path);
                painter.drawImage(0, 0, scaledImg);
                avatarLabel->setPixmap(QPixmap::fromImage(result));
            }
            else {
                avatarLabel->setText("👤");
                avatarLabel->setFont(QFont("Microsoft YaHei", 36));
            }

            form->addRow("姓名:", new QLabel(QString("<b>%1</b>").arg(m_currentTarget), &dialog));
            form->addRow("部门:", new QLabel(d, &dialog));
            form->addRow("职务:", new QLabel(j.isEmpty() ? "未分配" : j, &dialog));
            form->addRow("性别:", new QLabel(g.isEmpty() ? "未知" : g, &dialog));
            form->addRow("电话:", new QLabel(p.isEmpty() ? "未设置" : p, &dialog));
        }
        else {
            avatarLabel->setText("❌");
            form->addRow(new QLabel("无法获取该用户信息", &dialog));
        }

        layout->addWidget(avatarLabel, 0, Qt::AlignHCenter);
        layout->addLayout(form);
        layout->addStretch();

        QPushButton* closeBtn = new QPushButton("关闭名片", &dialog);
        closeBtn->setMinimumHeight(40);
        closeBtn->setStyleSheet("QPushButton { background-color: #3370FF; color: white; border-radius: 6px; } QPushButton:hover { background-color: #4E83FF; }");
        layout->addWidget(closeBtn);
        connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

        dialog.exec();
    }
}

void ChatModule::sendSystemMessage(const QString& to, const QString& msg) {
    if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject json;
        json["type"] = "chat";
        json["from"] = m_myName;
        json["to"] = to;
        json["msg"] = msg;
        m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
        m_chatHistories[to] += "<div style='text-align:right; margin-bottom:10px;'><span style='background-color:#95EC69; padding:8px; border-radius:6px; color:#000;'>系统提示: " + msg + "</span></div>";
        if (m_currentTarget == to) m_textBrowser->setHtml(m_chatHistories[to]);
    }
}

void ChatModule::onConnected() {
    QJsonObject json;
    json["type"] = "login";
    json["name"] = m_myName;
    m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
}

void ChatModule::onDisconnected() { m_targetLabel->setText("网络已断开"); }

void ChatModule::sendBroadcast(const QString& msg) {
    if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject json;
        json["type"] = "broadcast";
        json["from"] = m_myName;
        json["msg"] = msg;
        m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
    }
}
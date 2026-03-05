#include "ChatModule.h"
#include <QDateTime>
#include <QMessageBox>
#include <QTimer> 
#include <QSqlQuery>
#include <QSqlError>
#include <QFileDialog>
#include <QFileInfo>
#include <QAction>
#include <QScrollBar>
#include <QDesktopServices> // 新增：用于调用系统软件打开文件
#include <QUrl>
#include <QDir>
#include <QDialog>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>

// 构造函数：绑定聊天界面相关的UI组件与事件
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

    // ==========================================
    // 🚀 核心修复：防止文件变乱码的终极方案
    // ==========================================
    // 1. 彻底禁止文本框自己去加载本地文件（防止把PDF源码印在聊天框里）
    m_textBrowser->setOpenLinks(false);
    m_textBrowser->setOpenExternalLinks(false);

    // 2. 拦截点击事件，强行抛给 Windows 操作系统处理
    connect(m_textBrowser, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        QDesktopServices::openUrl(url); // 操作系统会自动用对应的软件打开该文件
        });

    // 网络信号绑定
    connect(m_tcpSocket, &QTcpSocket::connected, this, &ChatModule::onConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ChatModule::onDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ChatModule::onReadyRead);

    // 列表切换绑定
    connect(m_contactsList, &QListWidget::currentRowChanged, this, &ChatModule::onContactSwitched);

    // 辅助功能按钮绑定
    if (m_btnEmoji) connect(m_btnEmoji, &QPushButton::clicked, this, &ChatModule::onBtnEmojiClicked);
    if (m_btnFolder) connect(m_btnFolder, &QPushButton::clicked, this, &ChatModule::onBtnFolderClicked);
    if (m_btnHistory) connect(m_btnHistory, &QPushButton::clicked, this, &ChatModule::onBtnHistoryClicked);
    if (m_btnMoreOpt) connect(m_btnMoreOpt, &QPushButton::clicked, this, &ChatModule::onBtnMoreOptClicked);

    m_targetLabel->setText("请在左侧选择联系人开始聊天");
}

// 连接服务器：保存个人信息并触发联系人列表加载
void ChatModule::connectToServer(const QString& ip, quint16 port, const QString& myName) {
    m_myName = myName;
    loadContactsFromDatabase();
    m_tcpSocket->connectToHost(ip, port);
}

// 载入联系人：部门群权限隔离与加载
void ChatModule::loadContactsFromDatabase() {
    m_contactsList->clear();

    QString myDept;
    QString mySql = QString("SELECT id, department FROM users WHERE name = '%1'").arg(m_myName);
    QSqlQuery myQuery(mySql);
    if (myQuery.next()) {
        m_myEmpId = myQuery.value(0).toString();
        myDept = myQuery.value(1).toString();
    }

    // 1. 固定添加【公司总群】到列表顶部
    QListWidgetItem* allGroupItem = new QListWidgetItem("📢 【公司总群】");
    allGroupItem->setData(Qt::UserRole, "GROUP_公司总群");
    m_contactsList->addItem(allGroupItem);

    // 2. 部门群权限逻辑：总裁办可以看到所有部门的群，其他部门只能看自己的群
    QString deptSql;
    if (myDept == "总裁办") {
        deptSql = "SELECT DISTINCT department FROM users WHERE department != '' AND department IS NOT NULL";
    }
    else {
        deptSql = QString("SELECT DISTINCT department FROM users WHERE department = '%1'").arg(myDept);
    }

    QSqlQuery deptQuery(deptSql);
    while (deptQuery.next()) {
        QString dept = deptQuery.value(0).toString();
        QListWidgetItem* item = new QListWidgetItem(QString("👨‍👩‍👧‍👦 【部门群】%1").arg(dept));
        item->setData(Qt::UserRole, "GROUP_" + dept);
        m_contactsList->addItem(item);
    }

    // 3. 加载个人联系人列表，严格屏蔽超级管理员账号（注意 QString.arg 中 % 要用 %% 转义）
    QString sql = QString("SELECT id, name, department, role FROM users WHERE name != '%1' AND account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'").arg(m_myName);
    QSqlQuery query(sql);

    while (query.next()) {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString().trimmed();
        QString dept = query.value(2).toString().trimmed();
        QString role = query.value(3).toString().trimmed();

        QString formattedId = QString("%1").arg(id, 3, 10, QChar('0'));
        QString icon = (role == "管理员") ? "👨‍💼" : "👨‍💻";
        QListWidgetItem* item = new QListWidgetItem(QString("%1 %2 [%3] (%4)").arg(icon, name, formattedId, dept));

        item->setData(Qt::UserRole, name);
        m_contactsList->addItem(item);
    }
}

// 切换联系人槽函数
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

// 发送文本消息
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

// 文件/图片发送逻辑
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

        // 生成发件人本机的可点击 URL 链接
        QString fileUrl = QUrl::fromLocalFile(filePath).toString();

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

// 接收逻辑处理
void ChatModule::onReadyRead() {
    while (m_tcpSocket->canReadLine()) {
        QByteArray data = m_tcpSocket->readLine();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) continue;

        QJsonObject json = doc.object();
        QString type = json["type"].toString();
        QString fromUser = json["from"].toString();

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

            // 在本地缓存目录下自动保存收到的文件
            QDir dir("ChatFiles");
            if (!dir.exists()) dir.mkpath(".");

            QString localFileName = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + originalFileName;
            QString localFilePath = QDir::currentPath() + "/ChatFiles/" + localFileName;

            QFile localFile(localFilePath);
            if (localFile.open(QIODevice::WriteOnly)) {
                localFile.write(QByteArray::fromBase64(content.toUtf8()));
                localFile.close();
            }

            // 生成本地安全外链 URL
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

// 弹出表情选择菜单
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

// ==========================================
// 🚀 核心升级：实现“...”按钮的功能，区分群聊与私聊展示
// ==========================================
void ChatModule::onBtnMoreOptClicked() {
    if (m_currentTarget.isEmpty()) {
        QMessageBox::warning(nullptr, "提示", "请先选择聊天对象！");
        return;
    }

    QDialog dialog(nullptr);
    dialog.setMinimumSize(320, 450);

    if (m_isCurrentGroup) {
        // --- 逻辑分支 1：群聊，展示群成员列表 ---
        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QListWidget* listWidget = new QListWidget(&dialog);
        listWidget->setStyleSheet("QListWidget::item { height: 40px; font-size: 14px; border-bottom: 1px solid #F0F0F0; }");
        layout->addWidget(listWidget);

        QString sql;
        if (m_currentTarget == "公司总群") {
            sql = "SELECT name, department, job_title FROM users WHERE account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'";
        }
        else {
            sql = QString("SELECT name, department, job_title FROM users WHERE department = '%1' AND account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'").arg(m_currentTarget);
        }

        QSqlQuery query(sql);
        int count = 0;
        while (query.next()) {
            QString n = query.value(0).toString();
            QString d = query.value(1).toString();
            QString j = query.value(2).toString();
            if (j.isEmpty() || j == "未分配") j = "员工";
            listWidget->addItem(QString("👤 %1 (%2 - %3)").arg(n, d, j));
            count++;
        }

        dialog.setWindowTitle(QString("群成员列表 - %1 (%2人)").arg(m_currentTarget).arg(count));

        QPushButton* closeBtn = new QPushButton("关闭", &dialog);
        closeBtn->setMinimumHeight(35);
        layout->addWidget(closeBtn);
        connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

        dialog.exec();
    }
    else {
        // --- 逻辑分支 2：私聊，展示对方的个人名片信息 ---
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

        QString sql = QString("SELECT department, job_title, gender, phone, avatar FROM users WHERE name = '%1'").arg(m_currentTarget);
        QSqlQuery query(sql);
        if (query.next()) {
            QString d = query.value(0).toString();
            QString j = query.value(1).toString();
            QString g = query.value(2).toString();
            QString p = query.value(3).toString();
            QString avatarBase64 = query.value(4).toString();

            // 解析对方的 Base64 头像，如果有的话裁剪为圆形
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

// 系统通知
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
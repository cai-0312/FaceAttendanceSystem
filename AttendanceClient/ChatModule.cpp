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
#include <QUuid>
#include <QSettings> 
#include <QSplitter> 
#include <QMenu>
#include <QClipboard>
#include <QApplication>

// 🚀 核心通讯组件：通过短连接获取联系人列表和历史消息
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
            while (socket.waitForReadyRead(100) || socket.bytesAvailable() > 0) {
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

    // 允许文本选择和链接点击
    m_textBrowser->setOpenLinks(false);
    m_textBrowser->setOpenExternalLinks(false);
    m_textBrowser->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);

    // 🚀 核心拦截器：处理文件打开 与 单条记录删除的链接点击
    connect(m_textBrowser, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        QString urlStr = url.toString();
        if (urlStr.startsWith("del:")) {
            QString hash = urlStr.mid(4);
            int ret = QMessageBox::question(nullptr, "删除确认", "确定要删除这条记录吗？", QMessageBox::Yes | QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                QSettings settings("ChatLocalSettings.ini", QSettings::IniFormat);
                QStringList hidden = settings.value("HiddenMessages").toStringList();
                if (!hidden.contains(hash)) {
                    hidden.append(hash);
                    settings.setValue("HiddenMessages", hidden);
                }
                // 瞬间重绘画布，隐藏该记录
                int row = m_contactsList->currentRow();
                if (row >= 0) onContactSwitched(row);
            }
        }
        else {
            QDesktopServices::openUrl(url); // 打开附件或原图
        }
        });

    // ==============================================================================
    // 🚀 高级右键菜单：支持“复制”、“单条消息删除”
    // ==============================================================================
    m_textBrowser->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_textBrowser, &QTextBrowser::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (m_currentTarget.isEmpty()) return;

        QMenu menu;
        menu.setStyleSheet(
            "QMenu { background-color: #FFFFFF; border: 1px solid #DCDFE6; border-radius: 6px; padding: 4px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }"
            "QMenu::item { padding: 8px 25px 8px 15px; font-size: 14px; color: #303133; border-radius: 4px; margin: 2px 4px; }"
            "QMenu::item:selected { background-color: #165DFF; color: white; font-weight: bold; }"
            "QMenu::item:disabled { color: #C0C4CC; background-color: transparent; }"
            "QMenu::separator { height: 1px; background: #EBEEF5; margin: 4px 8px; }"
        );

        QAction* actCopy = menu.addAction("📄 复制选中文本");
        actCopy->setEnabled(m_textBrowser->textCursor().hasSelection());
        connect(actCopy, &QAction::triggered, m_textBrowser, &QTextBrowser::copy);

        QString anchor = m_textBrowser->anchorAt(pos);
        if (!anchor.isEmpty() && !anchor.startsWith("del:")) {
            QAction* actCopyPath = menu.addAction("📁 复制附件路径");
            connect(actCopyPath, &QAction::triggered, [anchor]() {
                QApplication::clipboard()->setText(QUrl(anchor).toLocalFile());
                });
        }

        // 如果右键正好点击在了消息自带的 [删除] 锚点区域，提供快捷删除菜单
        if (anchor.startsWith("del:")) {
            menu.addSeparator();
            QAction* actDelMsg = menu.addAction("🗑️ 删除该条记录 (仅本地)");
            connect(actDelMsg, &QAction::triggered, [this, anchor]() {
                QString hash = anchor.mid(4);
                QSettings settings("ChatLocalSettings.ini", QSettings::IniFormat);
                QStringList hidden = settings.value("HiddenMessages").toStringList();
                if (!hidden.contains(hash)) {
                    hidden.append(hash);
                    settings.setValue("HiddenMessages", hidden);
                }
                int row = m_contactsList->currentRow();
                if (row >= 0) onContactSwitched(row);
                });
        }

        menu.addSeparator();
        QAction* actSelectAll = menu.addAction("🔳 全选所有内容");
        connect(actSelectAll, &QAction::triggered, m_textBrowser, &QTextBrowser::selectAll);

        menu.exec(m_textBrowser->mapToGlobal(pos));
        });
    // ==============================================================================

    connect(m_tcpSocket, &QTcpSocket::connected, this, &ChatModule::onConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ChatModule::onDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ChatModule::onReadyRead);

    connect(m_contactsList, &QListWidget::currentRowChanged, this, &ChatModule::onContactSwitched);

    if (m_btnEmoji) connect(m_btnEmoji, &QPushButton::clicked, this, &ChatModule::onBtnEmojiClicked);
    if (m_btnFolder) connect(m_btnFolder, &QPushButton::clicked, this, &ChatModule::onBtnFolderClicked);

    // 🚀 彻底删除原先的清屏按钮逻辑及UI元素
    if (m_btnHistory) {
        m_btnHistory->hide();
        m_btnHistory->deleteLater();
        m_btnHistory = nullptr;
    }

    if (m_btnMoreOpt) connect(m_btnMoreOpt, &QPushButton::clicked, this, &ChatModule::onBtnMoreOptClicked);

    m_targetLabel->setText("请在左侧选择联系人开始聊天");

    if (m_searchEdit) {
        m_searchEdit->setPlaceholderText("🔍 搜索联系人...");
        connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& keyword) {
            QString kw = keyword.trimmed().toLower();
            for (int i = 0; i < m_contactsList->count(); ++i) {
                QListWidgetItem* item = m_contactsList->item(i);
                if (kw.isEmpty()) {
                    item->setHidden(false);
                }
                else {
                    bool match = item->text().toLower().contains(kw);
                    item->setHidden(!match);
                }
            }
            });
    }

    // 🚀 让聊天展示区与输入区能够自由上下拉伸调整大小
    QWidget* rightPanel = m_textBrowser->parentWidget();
    if (rightPanel) {
        QLayout* oldLayout = rightPanel->layout();
        if (oldLayout && oldLayout->inherits("QVBoxLayout")) {
            QVBoxLayout* vLayout = qobject_cast<QVBoxLayout*>(oldLayout);
            if (vLayout) {
                QSplitter* vSplitter = new QSplitter(Qt::Vertical, rightPanel);
                vSplitter->addWidget(m_textBrowser);
                QWidget* inputContainer = m_lineEdit->parentWidget();
                if (inputContainer && inputContainer != rightPanel) {
                    vSplitter->addWidget(inputContainer);
                }
                else {
                    vSplitter->addWidget(m_lineEdit);
                }
                vSplitter->setStretchFactor(0, 8);
                vSplitter->setStretchFactor(1, 2);
                vLayout->insertWidget(vLayout->indexOf(m_textBrowser), vSplitter);
                vSplitter->setStyleSheet("QSplitter::handle { background-color: #E5E6EB; height: 3px; } "
                    "QSplitter::handle:hover { background-color: #165DFF; }");
            }
        }
    }
}

void ChatModule::connectToServer(const QString& ip, quint16 port, const QString& myName) {
    m_myName = myName;
    loadContactsFromDatabase();
    m_tcpSocket->connectToHost(ip, port);
}

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
        QString icon = (role.contains("管理员")) ? "👨‍💼" : "👨‍💻";

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

    QJsonObject histReq;
    histReq["type"] = "query_chat_history";
    histReq["me"] = m_myName;
    histReq["target"] = m_currentTarget;
    histReq["is_group"] = m_isCurrentGroup;
    QJsonObject histRes = requestDataFromServer(histReq);

    m_chatHistories[m_currentTarget] = "";

    // 获取隐形的 Hash 列表
    QSettings settings("ChatLocalSettings.ini", QSettings::IniFormat);
    QStringList hiddenHashes = settings.value("HiddenMessages").toStringList();

    if (histRes["status"].toString() == "success") {
        QJsonArray dataArr = histRes["data"].toArray();
        for (int i = 0; i < dataArr.size(); ++i) {
            QJsonObject o = dataArr[i].toObject();
            QString sender = o["sender"].toString();
            QString content = o["content"].toString();
            QString timeStr = o["time"].toString();
            QString mType = o["msg_type"].toString();
            QString fName = o["filename"].toString();

            // 🚀 生成当前消息的唯一身份指纹 (Hash)
            QString msgHash = QString::number(qHash(timeStr + sender + content));

            // 如果该指纹在隐藏列表中，直接跳过不渲染！
            if (hiddenHashes.contains(msgHash)) continue;

            QString bubbleHtml;
            QString displayMsg;

            if (mType.contains("image") || mType.contains("file")) {
                QString suffix = fName.split(".").last().toLower();
                if (suffix.isEmpty()) suffix = "png";

                QString myLocalFolder = this->property("localFolder").toString();
                if (myLocalFolder.isEmpty()) myLocalFolder = "Unknown_User";
                QString clientDirPath = QCoreApplication::applicationDirPath() + "/ChatFiles/client/" + myLocalFolder;
                QDir dir(clientDirPath);
                if (!dir.exists()) dir.mkpath(".");

                QString localFileName = "hist_" + fName;
                QString localFilePath = clientDirPath + "/" + localFileName;

                if (!QFile::exists(localFilePath) && !content.isEmpty()) {
                    QFile localFile(localFilePath);
                    if (localFile.open(QIODevice::WriteOnly)) {
                        localFile.write(QByteArray::fromBase64(content.toUtf8()));
                        localFile.close();
                    }
                }

                QString fileUrl = QUrl::fromLocalFile(localFilePath).toString();
                if (mType.contains("image")) {
                    displayMsg = QString("<a href='%1'><img src='data:image/%2;base64,%3' width='150' style='border-radius:6px;' /></a><br><a href='%1' style='font-size:12px;color:gray;text-decoration:none;'>(点击外部查看原图)</a>").arg(fileUrl, suffix, content);
                }
                else {
                    displayMsg = QString("<a href='%1' style='text-decoration:none; color:#3370FF;'>📁 历史附件: %2<br><span style='font-size:12px;'>(点击使用系统软件打开)</span></a>").arg(fileUrl, fName);
                }
            }
            else {
                displayMsg = content.toHtmlEscaped();
            }

            // 🚀 在渲染时，给每一条消息的头部附带一个隐形的 Hash 专属删除按钮
            if (sender == m_myName) {
                QString header = QString("<a href='del:%1' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-right:10px;'>[删除]</a>"
                    "<span style='color:#999999; font-size:12px;'>%2 [我]</span>").arg(msgHash, timeStr);
                bubbleHtml = QString(
                    "<div style='text-align:right; margin-bottom:10px;'>"
                    "%1<br>"
                    "<span style='background-color:#95EC69; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#000;'>%2</span>"
                    "</div>"
                ).arg(header, displayMsg);
            }
            else {
                QString header = QString("<span style='color:#999999; font-size:12px;'>[%1] %2</span>"
                    "<a href='del:%3' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-left:10px;'>[删除]</a>").arg(sender, timeStr, msgHash);
                bubbleHtml = QString(
                    "<div style='text-align:left; margin-bottom:10px;'>"
                    "%1<br>"
                    "<span style='background-color:#FFFFFF; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; border:1px solid #EAEAEA; color:#000;'>%2</span>"
                    "</div>"
                ).arg(header, displayMsg);
            }
            m_chatHistories[m_currentTarget] += bubbleHtml;
        }
    }

    if (m_chatHistories[m_currentTarget].isEmpty()) {
        m_textBrowser->setHtml("<div style='text-align:center; color:#AAAAAA; margin-top:10px;'>暂无聊天记录</div>");
    }
    else {
        m_textBrowser->setHtml(m_chatHistories.value(m_currentTarget, ""));
    }
    m_textBrowser->moveCursor(QTextCursor::End);
}

void ChatModule::sendMessage() {
    QString msg = m_lineEdit->text().trimmed();
    if (msg.isEmpty() || m_currentTarget.isEmpty()) return;

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString readStatus = m_isCurrentGroup ? "" : QString("<span style='color:#AAAAAA; font-size:12px;'> (未读)</span>");

    // 刚发出去的消息同样生成指纹，允许实时删除
    QString msgHash = QString::number(qHash(timeStr + m_myName + msg));
    QString header = QString("<a href='del:%1' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-right:10px;'>[删除]</a>"
        "<span style='color:#999999; font-size:12px;'>%2 [我] </span> %3").arg(msgHash, timeStr, readStatus);

    QString myMsgHtml = "<div style='text-align:right; margin-bottom:10px;'>"
        + header + "<br>"
        "<span style='background-color:#95EC69; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#000000;'>"
        + msg.toHtmlEscaped() + "</span></div>";

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
    // 🚀 解除限制：提升到 100MB 允许绝大多数文件传输
    if (file.size() > 100 * 1024 * 1024) {
        QMessageBox::warning(nullptr, "超限", "为保证网络和内存稳定，单次限制发送 100MB 内的文件！");
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

        QString msgHash = QString::number(qHash(timeStr + m_myName + base64Data));
        QString header = QString("<a href='del:%1' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-right:10px;'>[删除]</a>"
            "<span style='color:#999999; font-size:12px;'>%2 [我] </span>").arg(msgHash, timeStr);

        QString myMsgHtml = "<div style='text-align:right; margin-bottom:10px;'>"
            + header + "<br>"
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
                "📢 <b>全局系统广播</b> [" + fromUser + "] " + timeStr + "<br><br>" + content.toHtmlEscaped() + "</span></div>";

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
            htmlContent = content.toHtmlEscaped(); // 保护表情包
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

        QString msgHash = QString::number(qHash(timeStr + fromUser + content));
        QString header = QString("<span style='color:#999999; font-size:12px;'>[%1] %2 %3</span>"
            "<a href='del:%4' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-left:10px;'>[删除]</a>").arg(fromUser, timeStr, offlineTag, msgHash);

        QString receiveHtml = "<div style='text-align:left; margin-bottom:10px;'>"
            + header + "<br>"
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
    QStringList emojis = { "😀", "😂", "😶", "😊", "😍", "😭", "😡", "👍", "🙏", "🎉", "🔥", "💼", "🏢", "☕", "❌", "✔️" };
    for (const auto& em : emojis) {
        QAction* act = emojiMenu.addAction(em);
        connect(act, &QAction::triggered, this, [=]() { m_lineEdit->insert(em); });
    }
    emojiMenu.setStyleSheet("QMenu { font-size: 20px; padding: 5px; } QMenu::item { padding: 8px; }");
    emojiMenu.exec(QCursor::pos());
}

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
        req["type"] = "query_user_profile";
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
        m_chatHistories[to] += "<div style='text-align:right; margin-bottom:10px;'><span style='background-color:#95EC69; padding:8px; border-radius:6px; color:#000;'>系统提示: " + msg.toHtmlEscaped() + "</span></div>";
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
#include "ChatModule.h"
#include "NetworkHelper.h"
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

// 构造函数：初始化网络模块、绑定UI交互事件，并对聊天显示框的富文本行为进行定制化配置
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

    // 禁用文本浏览器的默认外部链接打开行为，改为拦截并自定义处理
    m_textBrowser->setOpenLinks(false);
    m_textBrowser->setOpenExternalLinks(false);
    m_textBrowser->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);

    // 处理聊天记录中的超链接点击事件（如本地删除记录或打开文件）
    connect(m_textBrowser, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        QString urlStr = url.toString();
        // 识别特定前缀实现本地隐藏某条聊天记录的功能
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
                // 刷新当前聊天页面以隐藏被标记的记录
                int row = m_contactsList->currentRow();
                if (row >= 0) onContactSwitched(row);
            }
        }
        else {
            // 普通的文件或外部链接调用系统默认程序打开
            QDesktopServices::openUrl(url);
        }
        });

    // 聊天显示区域的右键菜单定制化配置
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

        // 复制纯文本菜单项
        QAction* actCopy = menu.addAction("📄 复制选中文本");
        actCopy->setEnabled(m_textBrowser->textCursor().hasSelection());
        connect(actCopy, &QAction::triggered, m_textBrowser, &QTextBrowser::copy);

        // 如果右击点存在超链接且非删除指令，则提供复制路径功能
        QString anchor = m_textBrowser->anchorAt(pos);
        if (!anchor.isEmpty() && !anchor.startsWith("del:")) {
            QAction* actCopyPath = menu.addAction("📁 复制附件路径");
            connect(actCopyPath, &QAction::triggered, [anchor]() {
                QApplication::clipboard()->setText(QUrl(anchor).toLocalFile());
                });
        }

        // 如果右击点包含删除指令标记，提供删除菜单项
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

    // 绑定TCP网络通讯的底层事件
    connect(m_tcpSocket, &QTcpSocket::connected, this, &ChatModule::onConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ChatModule::onDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ChatModule::onReadyRead);

    // 绑定左侧联系人列表的点击切换事件
    connect(m_contactsList, &QListWidget::currentRowChanged, this, &ChatModule::onContactSwitched);

    // 绑定功能按钮点击事件
    if (m_btnEmoji) connect(m_btnEmoji, &QPushButton::clicked, this, &ChatModule::onBtnEmojiClicked);
    if (m_btnFolder) connect(m_btnFolder, &QPushButton::clicked, this, &ChatModule::onBtnFolderClicked);

    // 绑定历史表情记录按钮点击事件
    if (m_btnHistory) connect(m_btnHistory, &QPushButton::clicked, this, &ChatModule::onBtnHistoryClicked);

    if (m_btnMoreOpt) connect(m_btnMoreOpt, &QPushButton::clicked, this, &ChatModule::onBtnMoreOptClicked);

    m_targetLabel->setText("请在左侧选择联系人开始聊天");

    // 配置联系人过滤搜索框逻辑
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

    // 重构右侧聊天面板布局：利用QSplitter分离聊天记录区和输入区，允许拖拽调节高度
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

// 连接即时通讯服务端
void ChatModule::connectToServer(const QString& ip, quint16 port, const QString& myName) {
    m_myName = myName;
    loadContactsFromDatabase();
    m_tcpSocket->connectToHost(ip, port);
}

// 通过短连接请求从服务器加载公司部门群组和员工列表信息
void ChatModule::loadContactsFromDatabase() {
    m_contactsList->clear();

    QJsonObject req;
    req["type"] = "query_chat_contacts";
    req["name"] = m_myName;
    QJsonObject res = NetworkHelper::request(req);

    if (res["status"].toString() != "success") return;

    QString myDept = res["my_dept"].toString();
    QString myLocalFolder = res["my_folder"].toString();
    this->setProperty("localFolder", myLocalFolder);

    // 添加公司总群标识
    QListWidgetItem* allGroupItem = new QListWidgetItem("📢 【公司总群】");
    allGroupItem->setData(Qt::UserRole, "GROUP_公司总群");
    m_contactsList->addItem(allGroupItem);

    // 遍历并添加各部门群聊
    QJsonArray deptArr = res["departments"].toArray();
    for (int i = 0; i < deptArr.size(); ++i) {
        QString dept = deptArr[i].toString();
        QListWidgetItem* item = new QListWidgetItem(QString("👨‍👩‍👧‍👦 【部门群】%1").arg(dept));
        item->setData(Qt::UserRole, "GROUP_" + dept);
        m_contactsList->addItem(item);
    }

    // 遍历并添加全部员工节点
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

// 切换聊天对象的上下文逻辑，包含渲染历史数据并处理已读回执
void ChatModule::onContactSwitched(int currentRow) {
    if (currentRow < 0) return;
    QString targetData = m_contactsList->item(currentRow)->data(Qt::UserRole).toString();

    // 区分群聊和私聊并更新界面标题
    if (targetData.startsWith("GROUP_")) {
        m_isCurrentGroup = true;
        m_currentTarget = targetData.replace("GROUP_", "");
        m_targetLabel->setText("正在与 【群聊：" + m_currentTarget + "】 聊天");
    }
    else {
        m_isCurrentGroup = false;
        m_currentTarget = targetData;
        m_targetLabel->setText("正在与 【" + m_currentTarget + "】 聊天");

        // 发送已读回执状态
        if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject json;
            json["type"] = "read_receipt";
            json["from"] = m_myName;
            json["to"] = m_currentTarget;
            m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
        }
    }

    // 从服务端拉取该联系人的历史聊天记录
    QJsonObject histReq;
    histReq["type"] = "query_chat_history";
    histReq["me"] = m_myName;
    histReq["target"] = m_currentTarget;
    histReq["is_group"] = m_isCurrentGroup;
    QJsonObject histRes = NetworkHelper::request(histReq);

    m_chatHistories[m_currentTarget] = "";

    // 读取本地的删除隐藏标记，避免再次渲染已删除的历史消息
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

            QString msgHash = QString::number(qHash(timeStr + sender + content));

            if (hiddenHashes.contains(msgHash)) continue;

            QString bubbleHtml;
            QString displayMsg;

            // 处理服务端返回的历史附件或图片缓存逻辑
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

                // 如果本地文件不存在则从Base64重新生成物理文件
                if (!QFile::exists(localFilePath) && !content.isEmpty()) {
                    QFile localFile(localFilePath);
                    if (localFile.open(QIODevice::WriteOnly)) {
                        localFile.write(QByteArray::fromBase64(content.toUtf8()));
                        localFile.close();
                    }
                }

                QString fileUrl = QUrl::fromLocalFile(localFilePath).toString();
                if (mType.contains("image")) {
                    displayMsg = QString("<a href='%1'><img src='data:image/%2;base64,%3' width='150' style='border-radius:6px;' /></a><br><a href='%1' style='font-size:12px;color:gray;text-decoration:none;'>(点击查看原图)</a>").arg(fileUrl, suffix, content);
                }
                else {
                    displayMsg = QString("<a href='%1' style='text-decoration:none; color:#3370FF;'>📁 历史附件: %2<br><span style='font-size:12px;'>(点击打开文件)</span></a>").arg(fileUrl, fName);
                }
            }
            else {
                displayMsg = content.toHtmlEscaped();
            }

            // 渲染自己发送的消息气泡
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
            // 渲染他人发送的消息气泡
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

    // 渲染最终结果并滚动到底部
    if (m_chatHistories[m_currentTarget].isEmpty()) {
        m_textBrowser->setHtml("<div style='text-align:center; color:#AAAAAA; margin-top:10px;'>暂无聊天记录</div>");
    }
    else {
        m_textBrowser->setHtml(m_chatHistories.value(m_currentTarget, ""));
    }
    m_textBrowser->moveCursor(QTextCursor::End);
}

// 组装文本消息数据包并投递至服务器，同步更新本地显示
void ChatModule::sendMessage() {
    QString msg = m_lineEdit->text().trimmed();
    if (msg.isEmpty() || m_currentTarget.isEmpty()) return;

    // --- 新增：提取并记录刚刚发送的表情 ---
    QStringList allEmojis = { "😀", "😂", "😶", "😊", "😍", "😭", "😡", "👍", "🙏", "🎉", "🔥", "💼", "🏢", "☕", "❌", "✔️" };
    for (const QString& em : allEmojis) {
        if (msg.contains(em)) {
            m_recentEmojis.removeAll(em); // 如果已经存在，先移除
            m_recentEmojis.prepend(em);   // 放到最前面（最新使用的）
        }
    }
    // 限制历史记录最多只保存最近的 10 个表情
    while (m_recentEmojis.size() > 10) {
        m_recentEmojis.removeLast();
    }

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString readStatus = m_isCurrentGroup ? "" : QString("<span style='color:#AAAAAA; font-size:12px;'> (未读)</span>");

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

    // 封包通过TCP发送
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

// 附件上传流程：选择物理文件、大小验证、本地拷贝缓存及 Base64 转码发送
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

    // 限制单次上传文件最大为100MB
    QFile file(filePath);
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

    // 拷贝文件至程序缓存目录
    QFile::copy(filePath, copiedFilePath);

    if (file.open(QIODevice::ReadOnly)) {
        QByteArray fileData;
        if (isImage) {
            // 对图片进行自动压缩处理以减少网络带压力
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

        // 构造网络请求并向服务端下发附件流
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
// 弹出最近发送过的历史表情菜单
void ChatModule::onBtnHistoryClicked() {
    // 判空提示
    if (m_recentEmojis.isEmpty()) {
        QMessageBox::information(nullptr, "提示", "您还没有发送过任何表情哦！");
        return;
    }

    // 动态构建历史表情菜单
    QMenu historyMenu;
    for (const QString& em : m_recentEmojis) {
        QAction* act = historyMenu.addAction(em);
        // 点击后将该表情插入到输入框中
        connect(act, &QAction::triggered, this, [=]() { m_lineEdit->insert(em); });
    }
    // 设置与普通表情包一致的放大样式
    historyMenu.setStyleSheet("QMenu { font-size: 20px; padding: 5px; } QMenu::item { padding: 8px; }");
    historyMenu.exec(QCursor::pos());
}

// 数据接收网关：持续读取网络报文，进行业务拆分和 UI 渲染调度
void ChatModule::onReadyRead() {
    while (m_tcpSocket->canReadLine()) {
        QByteArray data = m_tcpSocket->readLine();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) continue;

        QJsonObject json = doc.object();
        QString type = json["type"].toString();
        QString fromUser = json["from"].toString();

        // 业务分支 1：接收并渲染全局广播通知
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

        // 业务分支 2：处理已读回执，动态替换历史记录标记
        if (type == "read_receipt") {
            m_chatHistories[fromUser].replace("color:#AAAAAA; font-size:12px;'> (未读)</span>", "color:#409EFF; font-size:12px;'> (已读)</span>");
            if (m_currentTarget == fromUser && !m_isCurrentGroup) {
                m_textBrowser->setHtml(m_chatHistories[fromUser]);
            }
            continue;
        }

        // 业务分支 3：常规文本及文件接收处理
        QString targetTab = type.contains("group") ? json["department"].toString() : fromUser;
        QString content = json["msg"].toString();
        QString timeStr = json.contains("time") ? json["time"].toString() : QDateTime::currentDateTime().toString("HH:mm:ss");
        QString offlineTag = json["is_offline"].toBool() ? "<span style='color:#F56C6C;'>(离线转入)</span>" : "";

        QString htmlContent;
        if (type == "chat" || type == "group_chat") {
            htmlContent = content.toHtmlEscaped();
        }
        // 如果是文件载荷，在本地生成对应实体文件
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

        // 若正处于目标聊天界面，直接刷新并自动发送已读回执响应
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

// 弹出快捷表情选取菜单
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

// 更多功能菜单：根据当前窗口状态查看群成员列表或个人资料名片
void ChatModule::onBtnMoreOptClicked() {
    if (m_currentTarget.isEmpty()) {
        QMessageBox::warning(nullptr, "提示", "请先选择聊天对象！");
        return;
    }

    QDialog dialog(nullptr);
    dialog.setMinimumSize(320, 450);

    if (m_isCurrentGroup) {
        // 请求并渲染当前部门的群成员信息
        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QListWidget* listWidget = new QListWidget(&dialog);
        listWidget->setStyleSheet("QListWidget::item { height: 40px; font-size: 14px; border-bottom: 1px solid #F0F0F0; }");
        layout->addWidget(listWidget);

        QJsonObject req;
        req["type"] = "query_group_members";
        req["department"] = m_currentTarget;
        QJsonObject res = NetworkHelper::request(req);

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
        // 请求并渲染目标用户的个人头像与资料详情
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
        QJsonObject res = NetworkHelper::request(req);

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

// 通过网络接口向特定对象发送系统级别的结构化信息提示
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

// 监听到 Socket 连接成功时，发送初始化登录报文以校验身份
void ChatModule::onConnected() {
    QJsonObject json;
    json["type"] = "login";
    json["name"] = m_myName;
    m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
}

// 监听到 Socket 连接断开时的UI反馈
void ChatModule::onDisconnected() { m_targetLabel->setText("网络已断开"); }

// 发送全员或大范围通知使用的广播封装逻辑
void ChatModule::sendBroadcast(const QString& msg) {
    if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject json;
        json["type"] = "broadcast";
        json["from"] = m_myName;
        json["msg"] = msg;
        m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
    }
}

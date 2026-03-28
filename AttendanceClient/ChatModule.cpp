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
#include <QWidgetAction>
#include <QGridLayout>
#include <QScrollArea>
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
// 初始化网络模块、绑定UI交互事件，并对聊天显示框的富文本行为进行定制化配置
ChatModule::ChatModule(QListWidget* contactsList, QTextBrowser* textBrowser,
    QLineEdit* lineEdit, QLabel* targetLabel,
    QPushButton* btnEmoji, QPushButton* btnFolder,
    QPushButton* btnHistory, QPushButton* btnMoreOpt,
    QLineEdit* searchEdit, QObject* parent)
    : QObject(parent), m_contactsList(contactsList), m_textBrowser(textBrowser),
    m_lineEdit(lineEdit), m_targetLabel(targetLabel),
    m_btnEmoji(btnEmoji), m_btnFolder(btnFolder),
    m_btnHistory(btnHistory), m_btnMoreOpt(btnMoreOpt),
    m_searchEdit(searchEdit), m_isCurrentGroup(false)
{
    m_tcpSocket = new QTcpSocket(this);
    // 配置富文本浏览器：禁用默认超链接点击，拦截进行自定义处理
    m_textBrowser->setOpenLinks(false);
    m_textBrowser->setOpenExternalLinks(false);
    m_textBrowser->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    connect(m_textBrowser, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        QString urlStr = url.toString();
        // 捕获到内部删除标记时，执行本地隐藏清理逻辑
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
                int row = m_contactsList->currentRow();
                if (row >= 0) onContactSwitched(row);
            }
        }
        else {
            QDesktopServices::openUrl(url);
        }
        });
    // 为聊天窗口配置右键快捷菜单（支持复制、选择全选及本地附件路径复制）
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
        QAction* actCopy = menu.addAction("复制选中文本");
        actCopy->setEnabled(m_textBrowser->textCursor().hasSelection());
        connect(actCopy, &QAction::triggered, m_textBrowser, &QTextBrowser::copy);
        QString anchor = m_textBrowser->anchorAt(pos);
        if (!anchor.isEmpty() && !anchor.startsWith("del:")) {
            QAction* actCopyPath = menu.addAction("复制附件路径");
            connect(actCopyPath, &QAction::triggered, [anchor]() {
                QApplication::clipboard()->setText(QUrl(anchor).toLocalFile());
                });
        }
        if (anchor.startsWith("del:")) {
            menu.addSeparator();
            QAction* actDelMsg = menu.addAction("删除该条记录 (仅本地)");
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
        QAction* actSelectAll = menu.addAction("全选所有内容");
        connect(actSelectAll, &QAction::triggered, m_textBrowser, &QTextBrowser::selectAll);

        menu.exec(m_textBrowser->mapToGlobal(pos));
        });
    // 绑定底层的 TCP 状态及数据回调
    connect(m_tcpSocket, &QTcpSocket::connected, this, &ChatModule::onConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ChatModule::onDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ChatModule::onReadyRead);
    // 绑定界面控件的各种交互操作
    connect(m_contactsList, &QListWidget::currentRowChanged, this, &ChatModule::onContactSwitched);
    connect(m_btnEmoji, &QPushButton::clicked, this, &ChatModule::onBtnEmojiClicked);
    connect(m_btnFolder, &QPushButton::clicked, this, &ChatModule::onBtnFolderClicked);
    connect(m_btnHistory, &QPushButton::clicked, this, &ChatModule::onBtnHistoryClicked);
    connect(m_btnMoreOpt, &QPushButton::clicked, this, &ChatModule::onBtnMoreOptClicked);
    m_targetLabel->setText("请在左侧选择联系人开始聊天");
    // 绑定通讯录过滤搜索功能
    m_searchEdit->setPlaceholderText("搜索联系人...");
    QAction* searchAction = new QAction(m_searchEdit);
    searchAction->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_search.svg"));
    m_searchEdit->addAction(searchAction, QLineEdit::LeadingPosition);
    m_searchEdit->setStyleSheet(
        "QLineEdit { background: #F2F3F5; border: none; border-radius: 18px; padding-left: 5px; color: #1F2329; }"
        "QLineEdit:focus { background: #FFF; border: 1px solid #3370FF; }"
    );
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
    // ====================== 动态重构输入框：实现自由拉伸 ======================
    QWidget* mainPage = m_textBrowser->parentWidget(); // 获取聊天大屏的绝对父容器
    // 2. 创建上下分割器
    QSplitter* splitter = new QSplitter(Qt::Vertical, mainPage);
    splitter->setHandleWidth(4);
    splitter->setStyleSheet("QSplitter::handle { background-color: #DCDFE6; margin: 2px 0px; }");

    // 3. 将聊天大屏替换进分割器
    if (mainPage && mainPage->layout()) {
        mainPage->layout()->replaceWidget(m_textBrowser, splitter);
    }

    // 4. 构建底部容器 (包含工具栏、文本框和悬浮按钮)
    QWidget* bottomWidget = new QWidget(splitter);
    bottomWidget->setMinimumHeight(120); // 预留足够空间给拉伸
    QVBoxLayout* bottomLayout = new QVBoxLayout(bottomWidget);
    bottomLayout->setContentsMargins(0, 5, 0, 0);
    bottomLayout->setSpacing(5);
    // 注入工具栏 SVG 图标
    QString toolBtnStyle =
        "QPushButton { background: transparent; color: #4E5969; border-radius: 6px; padding: 6px 12px; font-weight: bold; border: none; font-size: 13px; }"
        "QPushButton:hover { background: #E5E6EB; color: #165DFF; }";
    // 1. 表情按钮 (清除原有硬编码的😀，并应用统一样式)
    m_btnEmoji->setText("");
    m_btnEmoji->setCursor(Qt::PointingHandCursor);
    m_btnEmoji->setStyleSheet(toolBtnStyle);
    m_btnEmoji->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_emoji.svg"));
     m_btnEmoji->setIconSize(QSize(18, 18));
    // 2. 文件按钮
    m_btnFolder->setText("");
    m_btnFolder->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_folder.svg"));
    m_btnFolder->setIconSize(QSize(18, 18));
    m_btnFolder->setCursor(Qt::PointingHandCursor);
    m_btnFolder->setStyleSheet(toolBtnStyle);
    // 3. 历史记录按钮
    m_btnHistory->setText("");
    m_btnHistory->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_history.svg"));
    m_btnHistory->setIconSize(QSize(18, 18));
    m_btnHistory->setCursor(Qt::PointingHandCursor);
    m_btnHistory->setStyleSheet(toolBtnStyle);
    // ── 顶部工具栏 layout (只放表情、文件夹、历史按钮) ────────────────
    QHBoxLayout* toolLayout = new QHBoxLayout();
    toolLayout->setContentsMargins(5, 5, 5, 5); // 增加呼吸感内边距
    toolLayout->setSpacing(8);                  // 控制按钮间的完美间距
    toolLayout->addWidget(m_btnEmoji);
    toolLayout->addWidget(m_btnFolder);
    toolLayout->addWidget(m_btnHistory);
    toolLayout->addStretch();                   // 弹簧：把三个按钮死死顶在左边，不再凌乱
    bottomLayout->addLayout(toolLayout);

    // ── 🚩 核心修改：文本框与发送按钮的叠加容器 ──
    QWidget* textEditWrapper = new QWidget(bottomWidget);
    QGridLayout* wrapperLayout = new QGridLayout(textEditWrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0); // Wrapper本身不需要间距
    wrapperLayout->setSpacing(0);
    m_textEdit = new QTextEdit(textEditWrapper);
    m_textEdit->setPlaceholderText("在此输入消息...");
    m_textEdit->setStyleSheet(
        "QTextEdit { border: 1px solid #DCDFE6; border-radius: 6px; padding: 10px 110px 42px 10px; background: white; color: #303133; font-size: 14px; }"
        "QTextEdit:focus { border-color: #409EFF; }"
    );
    m_textEdit->setMinimumHeight(80); // 增加最小高度，使其更协调
    m_textEdit->installEventFilter(this); // 保持键盘事件拦截 (Enter发送, Shift+Enter换行)

    QPushButton* btnSend = new QPushButton("发送", textEditWrapper);
    btnSend->setCursor(Qt::PointingHandCursor);
    btnSend->setToolTip("按 Enter 发送，Shift + Enter 换行");
    btnSend->setStyleSheet(
        "QPushButton { background-color: #3370FF; color: white; border-radius: 4px; padding: 8px 16px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background-color: #4E83FF; }"
        "QPushButton:pressed { background-color: #2458CC; }"
    );
    btnSend->setFixedSize(90, 32);
    wrapperLayout->addWidget(m_textEdit, 0, 0);
    wrapperLayout->addWidget(btnSend, 0, 0, Qt::AlignBottom | Qt::AlignRight);
    btnSend->setContentsMargins(15, 15, 15, 15);
    connect(btnSend, &QPushButton::clicked, this, &ChatModule::sendMessage);
    bottomLayout->addWidget(textEditWrapper, 1);

    // 5. 组装到分割器
    splitter->addWidget(m_textBrowser);
    splitter->addWidget(bottomWidget);
    splitter->setStretchFactor(0, 8); // 聊天大屏占 80%
    splitter->setStretchFactor(1, 2); // 输入区占 20%

    // 6. 🚩 终极毁灭旧控件：顺藤摸瓜，连同它外层的带边框壳子一起干掉！
    QWidget* oldContainer = m_lineEdit;
    // 向上寻找，直到找到它在 mainPage 下的直接子节点（也就是旧的整个底部栏外壳）
    while (oldContainer && oldContainer->parentWidget() != mainPage && oldContainer->parentWidget() != nullptr) {
        oldContainer = oldContainer->parentWidget();
    }
    // 彻底摧毁这个旧壳子（极度安全：因为按钮已经被我们提前移走了，所以只会删掉多余的空壳和旧编辑框）
    if (oldContainer && oldContainer != mainPage && oldContainer != m_textBrowser && oldContainer != splitter) {
        oldContainer->hide();
        oldContainer->deleteLater();
    }
    else {
        m_lineEdit->hide();
        m_lineEdit->deleteLater();
    }
    // ===================================================================
}
// 初始化 TCP 连接并加载云端联系人列表
void ChatModule::connectToServer(const QString& ip, quint16 port, const QString& myName) {
    m_myName = myName;
    loadContactsFromDatabase();
    m_tcpSocket->connectToHost(ip, port);
}
// 监听到 Socket 连接成功时，发送初始化登录报文以校验身份
void ChatModule::onConnected() {
    QJsonObject json;
    json["type"] = "login";
    json["name"] = m_myName;
    m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
}
// 监听到 Socket 连接断开时的状态清理
void ChatModule::onDisconnected() {
    m_targetLabel->setText("网络已断开");
}
// 从数据库请求最新的部门群组及员工档案数据，渲染到联系人列表中
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
    // 1. 插入全局广播总群 (使用大喇叭 SVG)
    QListWidgetItem* allGroupItem = new QListWidgetItem(" 【公司总群】");
    allGroupItem->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_megaphone.svg"));
    allGroupItem->setData(Qt::UserRole, "GROUP_公司总群");
    m_contactsList->addItem(allGroupItem);
    // 2. 插入部门群聊 (使用部门群组 SVG)
    QJsonArray deptArr = res["departments"].toArray();
    for (int i = 0; i < deptArr.size(); ++i) {
        QString dept = deptArr[i].toString();
        QListWidgetItem* item = new QListWidgetItem(QString(" 【部门群】%1").arg(dept));
        item->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_department_group.svg"));
        item->setData(Qt::UserRole, "GROUP_" + dept);
        m_contactsList->addItem(item);
    }
    // 3. 插入独立用户列表 (区分管理员与普通员工 SVG)
    QJsonArray userArr = res["users"].toArray();
    for (int i = 0; i < userArr.size(); ++i) {
        QJsonObject u = userArr[i].toObject();
        QString name = u["name"].toString();
        QString dept = u["department"].toString();
        QString role = u["role"].toString();
        QString jobTitle = u["job_title"].toString();
        if (jobTitle.isEmpty() || jobTitle == "未分配") jobTitle = "员工";
        QString formattedId = QString("%1").arg(u["id"].toInt(), 3, 10, QChar('0'));
        QListWidgetItem* item = new QListWidgetItem(
            QString(" %1 [%2] (%3-%4)").arg(name, formattedId, dept, jobTitle));
        // 动态判定图标
        if (role.contains("管理员")) {
            item->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_admin.svg"));
        }
        else {
            item->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_staff.svg"));
        }
        item->setData(Qt::UserRole, name);
        m_contactsList->addItem(item);
    }
    m_contactsList->setIconSize(QSize(24, 24));
}
// 切换左侧列表时的对话上下文处理（读取本地缓存与服务端历史）
void ChatModule::onContactSwitched(int currentRow) {
    if (currentRow < 0) return;
    QString targetData = m_contactsList->item(currentRow)->data(Qt::UserRole).toString();
    // 判断是部门群聊还是个人私聊
    if (targetData.startsWith("GROUP_")) {
        m_isCurrentGroup = true;
        m_currentTarget = targetData.replace("GROUP_", "");
        m_targetLabel->setText("正在与 【群聊：" + m_currentTarget + "】 聊天");
    }
    else {
        m_isCurrentGroup = false;
        m_currentTarget = targetData;
        m_targetLabel->setText("正在与 【" + m_currentTarget + "】 聊天");
        // 通知服务端更新已读状态
        if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject json;
            json["type"] = "read_receipt";
            json["from"] = m_myName;
            json["to"] = m_currentTarget;
            m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
        }
    }
    // 向服务端拉取该会话的完整历史记录
    QJsonObject histReq;
    histReq["type"] = "query_chat_history";
    histReq["me"] = m_myName;
    histReq["target"] = m_currentTarget;
    histReq["is_group"] = m_isCurrentGroup;
    QJsonObject histRes = NetworkHelper::request(histReq);
    m_chatHistories[m_currentTarget] = "";
    QSettings settings("ChatLocalSettings.ini", QSettings::IniFormat);
    QStringList hiddenHashes = settings.value("HiddenMessages").toStringList();
    // 渲染历史数据到展示界面
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
            // 处理附件或图片消息渲染
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
                    displayMsg = QString("<a href='%1'><img src='data:image/%2;base64,%3' width='150' style='border-radius:6px;' /></a><br><a href='%1' style='font-size:12px;color:gray;text-decoration:none;'>(点击查看原图)</a>").arg(fileUrl, suffix, content);
                }
                else {
                    displayMsg = QString("<a href='%1' style='text-decoration:none; color:#3370FF;'>历史附件: %2<br><span style='font-size:12px;'>(点击打开文件)</span></a>").arg(fileUrl, fName);
                }
            }
            // 处理纯文本消息渲染
            else {
                displayMsg = content.toHtmlEscaped();
            }
            int isRead = o["is_read"].toInt();
            if (sender == m_myName) {
                QString readStatusHtml = "";
                if (!m_isCurrentGroup) {
                    if (isRead == 0) {
                        readStatusHtml = "&nbsp;&nbsp;<span style='color:#909399;'>(未读)</span>";
                    }
                    else {
                        readStatusHtml = "&nbsp;&nbsp;<span style='color:#909399;'>(已读)</span>";
                    }
                }
                QString header = QString("<a href='del:%1' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-right:10px;'><img src='../../AttendanceClient/icon_library/Chat/btn_delete.svg' width='14' height='14' align='middle'> 删除</a>"
                    "<span style='color:#999999; font-size:12px;'>%2 [我]</span>%3").arg(msgHash, timeStr, readStatusHtml);

                bubbleHtml = QString(
                    "<div style='text-align:right; margin-bottom:10px;'>"
                    "%1<br>"
                    "<span style='background-color:#95EC69; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#000; text-align:left;'>%2</span>"
                    "</div>"
                ).arg(header, displayMsg);
            }
            else {
                QString header = QString("<span style='color:#999999; font-size:12px;'>[%1] %2</span>"
                    "<a href='del:%4' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-left:10px;'><img src='../../AttendanceClient/icon_library/Chat/btn_delete.svg' width='14' height='14' align='middle'> 删除</a>").arg(sender, timeStr, msgHash);
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
// 处理文本信息的发送及本地 UI 渲染
void ChatModule::sendMessage() {
    QString msg = m_textEdit->toPlainText().trimmed();
    if (msg.isEmpty() || m_currentTarget.isEmpty()) return;
    // 1. 同步扩充用于记录历史的动态 Emoji 库
    QStringList allEmojis = {
        "😀","😃","😄","😁","😆","😅","🤣","😂",
        "🙂","🙃","😉","😊","😇","🥰","😍","🤩",
        "😘","😗","😚","😙","😋","😛","😜","🤪",
        "😝","🤑","🤗","🤭","🤫","🤔","🤐","🤨",
        "😐","😑","😶","😏","😒","🙄","😬","🤥",
        "😌","😔","😪","🤤","😴","😷","🤒","🤕",
        "🤢","🤮","🤧","🥵","🥶","🥴","😵","🤯",
        "👍","👎","👏","🙌","👐","🤲","🤝","🙏",
        "💪","✨","🔥","🎉","💼","💻","☕","🚀"
    };
    for (const QString& em : allEmojis) {
        if (msg.contains(em)) {
            m_recentEmojis.removeAll(em);
            m_recentEmojis.prepend(em);
        }
    }
    while (m_recentEmojis.size() > 10) m_recentEmojis.removeLast();
    QString timeStr = QDateTime::currentDateTime().toString("MM-dd HH:mm");
    QString msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString readStatus = m_isCurrentGroup ? "" : QString("<span style='color:#AAAAAA; font-size:12px;'> (未读)</span>");
    // 2. 将硬编码的 [删除] 替换为 SVG 矢量图
    QString msgHash = QString::number(qHash(timeStr + m_myName + msg));
    QString header = QString("<a href='del:%1' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-right:10px;'>"
        "<img src='../../AttendanceClient/icon_library/Chat/btn_delete.svg' width='14' height='14' align='middle'> 删除</a>"
        "<span style='color:#999999; font-size:12px;'>%2 [我] </span> %3").arg(msgHash, timeStr, readStatus);

    QString myMsgHtml = "<div style='text-align:right; margin-bottom:10px;'>"
        + header + "<br>"
        "<span style='background-color:#95EC69; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#000000;'>"
        + msg.toHtmlEscaped() + "</span></div>";

    m_chatHistories[m_currentTarget] += myMsgHtml;
    m_textBrowser->setHtml(m_chatHistories[m_currentTarget]);
    m_textBrowser->moveCursor(QTextCursor::End);
    m_textEdit->clear();
    // 通过 TCP 发送 JSON 数据包至服务端
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
// 选择本地文件或图片并上传分发
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
    if (file.size() > 100 * 1024 * 1024) {
        QMessageBox::warning(nullptr, "超限", "为保证网络和内存稳定，单次限制发送 100MB 内的文件！");
        return;
    }
    QString myLocalFolder = this->property("localFolder").toString();
    if (myLocalFolder.isEmpty()) myLocalFolder = "Unknown_User";
    QString clientDirPath = QCoreApplication::applicationDirPath() + "/ChatFiles/client/" + myLocalFolder;
    QDir dir(clientDirPath);
    if (!dir.exists()) dir.mkpath(".");
    // 将选中的文件复制到客户端缓存目录
    QString localFileName = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + fi.fileName();
    QString copiedFilePath = clientDirPath + "/" + localFileName;
    QFile::copy(filePath, copiedFilePath);
    // 将二进制文件转换为 Base64，封装 JSON 网络请求发送
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
            displayContent = QString("<a href='%1' style='text-decoration:none; color:#3370FF;'>已发送文件: %2<br><span style='font-size:12px;'>(点击使用系统软件打开)</span></a>").arg(fileUrl, fi.fileName());
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
// 异步读取并解析网络传来的JSON通讯数据
void ChatModule::onReadyRead() {
    while (m_tcpSocket->canReadLine()) {
        QByteArray data = m_tcpSocket->readLine();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) continue;

        QJsonObject json = doc.object();
        QString type = json["type"].toString();
        QString fromUser = json["from"].toString();
        // 处理全局广播
        if (type == "broadcast") {
            QString content = json["msg"].toString();
            emit broadcastReceived(fromUser, content);

            QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
            QString receiveHtml = "<div style='text-align:center; margin-bottom:10px;'>"
                "<span style='background-color:#F56C6C; color:#FFF; padding:6px 12px; border-radius:6px; font-size:12px;'>"
                "<b>全局系统广播</b> [" + fromUser + "] " + timeStr + "<br><br>" + content.toHtmlEscaped() + "</span></div>";
            m_chatHistories["公司总群"] += receiveHtml;
            if (m_currentTarget == "公司总群") {
                m_textBrowser->setHtml(m_chatHistories["公司总群"]);
                m_textBrowser->moveCursor(QTextCursor::End);
            }
            continue;
        }
        // 处理已读回执
        if (type == "read_receipt") {
            m_chatHistories[fromUser].replace("color:#AAAAAA; font-size:12px;'> (未读)</span>", "color:#409EFF; font-size:12px;'> (已读)</span>");
            if (m_currentTarget == fromUser && !m_isCurrentGroup) {
                m_textBrowser->setHtml(m_chatHistories[fromUser]);
            }
            continue;
        }
        // 解析普通/群组消息的归属方
        QString targetTab = type.contains("group") ? json["department"].toString() : fromUser;
        QString content = json["msg"].toString();
        QString timeStr = json.contains("time") ? json["time"].toString() : QDateTime::currentDateTime().toString("HH:mm:ss");
        QString offlineTag = json["is_offline"].toBool() ? "<span style='color:#F56C6C;'>(离线转入)</span>" : "";
        QString htmlContent;
        if (type == "chat" || type == "group_chat") {
            htmlContent = content.toHtmlEscaped();
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
            // 落盘本地保存
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
                htmlContent = QString("<a href='%1' style='text-decoration:none; color:#3370FF;'>收到文件: %2<br><span style='font-size:12px;'>(点击使用系统软件打开)</span></a>").arg(fileUrl, originalFileName);
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
        // 如果正处于该聊天窗口则立即渲染并回执
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
// 唤出 Emoji 表情选择菜单
void ChatModule::onBtnEmojiClicked() {
    // 1. 创建自定义的无边框菜单
    QMenu* emojiMenu = new QMenu(m_textEdit);
    emojiMenu->setStyleSheet(
        "QMenu { background-color: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 8px; padding: 4px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }"
    );
    // 2. 创建网格容器
    QWidget* gridWidget = new QWidget(emojiMenu);
    QGridLayout* gridLayout = new QGridLayout(gridWidget);
    gridLayout->setSpacing(2);
    gridLayout->setContentsMargins(4, 4, 4, 4);
    // 3. 扩充我们的动态表情库 (Windows 会自动将它们渲染为微软原生彩色表情)
    QStringList dynamicEmojis = {
        "😀","😃","😄","😁","😆","😅","🤣","😂",
        "🙂","🙃","😉","😊","😇","🥰","😍","🤩",
        "😘","😗","😚","😙","😋","😛","😜","🤪",
        "😝","🤑","🤗","🤭","🤫","🤔","🤐","🤨",
        "😐","😑","😶","😏","😒","🙄","😬","🤥",
        "😌","😔","😪","🤤","😴","😷","🤒","🤕",
        "🤢","🤮","🤧","🥵","🥶","🥴","😵","🤯",
        "👍","👎","👏","🙌","👐","🤲","🤝","🙏",
        "💪","✨","🔥","🎉","💼","💻","☕","🚀"
    };
    // 4. 将表情动态生成为 8列 x 多行 的网格按钮
    int row = 0, col = 0;
    for (const QString& em : dynamicEmojis) {
        QPushButton* btn = new QPushButton(em, gridWidget);
        btn->setFixedSize(38, 38); // 扩大点击区域
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { font-size: 22px; border: none; background: transparent; border-radius: 8px; }"
            "QPushButton:hover { background-color: #F2F3F5; }"
        );
        // 点击后插入文本框，并关闭菜单
        connect(btn, &QPushButton::clicked, this, [=]() {
            m_textEdit->insertPlainText(em);
            emojiMenu->close();
            });
        gridLayout->addWidget(btn, row, col);
        col++;
        if (col >= 8) { // 每行限制 8 个表情
            col = 0;
            row++;
        }
    }
    // 5. 将网格容器包装进 QWidgetAction，使其能在 QMenu 中显示
    QWidgetAction* widgetAction = new QWidgetAction(emojiMenu);
    widgetAction->setDefaultWidget(gridWidget);
    emojiMenu->addAction(widgetAction);
    // 6. 精准计算弹出位置 (让面板在表情按钮的正上方弹出)
    QPoint btnPos = m_btnEmoji->mapToGlobal(QPoint(0, 0));
    int menuWidth = gridLayout->columnCount() * 40 + 8;
    int menuHeight = gridLayout->rowCount() * 40 + 8;
    QPoint popupPos(
        btnPos.x() - menuWidth / 2 + m_btnEmoji->width() / 2, // 水平居中对齐按钮
        btnPos.y() - menuHeight - 10                          // 垂直偏上 10px 悬浮
    );
    // 弹出自定义表情库面板
    emojiMenu->exec(popupPos);
    delete emojiMenu;
}
// 响应按钮操作：唤出历史常用表情快捷面板
void ChatModule::onBtnHistoryClicked() {
    if (m_recentEmojis.isEmpty()) {
        QMessageBox::information(nullptr, "提示", "您还没有发送过任何表情哦！");
        return;
    }
    QMenu historyMenu;
    for (const QString& em : m_recentEmojis) {
        QAction* act = historyMenu.addAction(em);
        connect(act, &QAction::triggered, this, [=]() { m_textEdit->insertPlainText(em); });
    }
    historyMenu.setStyleSheet("QMenu { font-size: 20px; padding: 5px; } QMenu::item { padding: 8px; }");
    historyMenu.exec(QCursor::pos());
}
// 更多选项菜单处理（弹窗展示对应的人员或群组信息）
void ChatModule::onBtnMoreOptClicked() {
    if (m_currentTarget.isEmpty()) {
        QMessageBox::warning(nullptr, "提示", "请先选择聊天对象！");
        return;
    }
    QDialog dialog(nullptr);
    dialog.setMinimumSize(320, 450);
    // 查询群组成员列表
    if (m_isCurrentGroup) {
        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QListWidget* listWidget = new QListWidget(&dialog);
        listWidget->setStyleSheet("QListWidget {""   background-color: #FFFFFF;" "   border: 1px solid #E0E0E0;""   border-radius: 8px;""   outline: none;""}"
            "QListWidget::item {""   height: 40px;""   color: #333333;" "   font-size: 14px;" "   padding-left: 12px;""   border-bottom: 1px solid #F0F0F0;" "}"
            "QListWidget::item:hover {""   background-color: #F5F5F5;" "}"
            "QListWidget::item:selected {""   background-color: #E3F2FD;" "   color: #1565C0;" "}");
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
                QString name = u["name"].toString();
                QString dept = u["dept"].toString();
                QString job = u["job"].toString();

                QListWidgetItem* item = new QListWidgetItem(QString("%1 (%2 - %3)").arg(name, dept, job));
                item->setData(Qt::UserRole, name); // 存储用户名
                listWidget->addItem(item);
                count++;
            }
        }

        dialog.setWindowTitle(QString("群成员列表 - %1 (%2人)").arg(m_currentTarget).arg(count));
        dialog.setStyleSheet("QDialog {""   background-color: #FFFFFF;""   border-radius: 10px;""}"
        );
        // 点击成员时显示个人信息
        connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            QString userName = item->data(Qt::UserRole).toString();
            if (!userName.isEmpty()) {
                showUserInfo(userName);
            }
            });
        QPushButton* closeBtn = new QPushButton("关闭", &dialog);
        closeBtn->setMinimumHeight(35);
        layout->addWidget(closeBtn);
        connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

        dialog.exec();
    }
    // 查询单独个人名片资料
    else {
        showUserInfo(m_currentTarget);
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
// 触发并发送全局系统广播消息
void ChatModule::sendBroadcast(const QString& msg) {
    if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject json;
        json["type"] = "broadcast";
        json["from"] = m_myName;
        json["msg"] = msg;
        m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
    }
}

void ChatModule::showUserInfo(const QString& userName)
{
    QDialog dialog(nullptr);
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
    req["name"] = userName;   // 使用传入的用户名
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
            avatarLabel->setText("");
            avatarLabel->setFont(QFont("Microsoft YaHei", 36));
        }

        form->addRow("姓名:", new QLabel(QString("<b>%1</b>").arg(userName), &dialog));
        form->addRow("部门:", new QLabel(d, &dialog));
        form->addRow("职务:", new QLabel(j.isEmpty() ? "未分配" : j, &dialog));
        form->addRow("性别:", new QLabel(g.isEmpty() ? "未知" : g, &dialog));
        form->addRow("电话:", new QLabel(p.isEmpty() ? "未设置" : p, &dialog));
    }
    else {
        avatarLabel->setText("");
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
bool ChatModule::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_textEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        // 捕捉回车键
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                // Shift + Enter：正常换行，放行
                return false;
            }
            else {
                // 纯 Enter：发送消息并拦截事件（防止光标乱跑）
                sendMessage();
                return true;
            }
        }
    }
    return QObject::eventFilter(obj, event);
}
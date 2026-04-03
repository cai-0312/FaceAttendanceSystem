#include "ChatModule.h"
#include "NetworkHelper.h"
#include <QDateTime>
#include <QMessageBox>
#include <QTimer> 
#include <QFileDialog>
#include <QFileInfo>
#include <QAction>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextFragment>
#include <QDesktopServices> 
#include <QCryptographicHash>
#include <QWidgetAction>
#include <QMimeData>
#include <QProcess>
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
#include <QFileIconProvider>
#include <QBuffer>
// 构造函数，初始化聊天界面、控件绑定和网络对象
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
    // 设置消息浏览器的交互方式
    m_textBrowser->setOpenLinks(false);
    m_textBrowser->setOpenExternalLinks(false);
    m_textBrowser->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    // 绑定链接点击处理，支持删除、下载和普通打开
    connect(m_textBrowser, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        QString urlStr = url.toString();
        // 删除本地聊天记录中的消息
        if (urlStr.startsWith("del:")) {
            QString hash = urlStr.mid(4);
            int ret = QMessageBox::question(nullptr, "删除确认", "确定要删除这条记录吗？", QMessageBox::Yes | QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                // 将消息哈希写入隐藏列表
                QSettings settings("ChatLocalSettings.ini", QSettings::IniFormat);
                QStringList hidden = settings.value("HiddenMessages").toStringList();
                if (!hidden.contains(hash)) {
                    hidden.append(hash);
                    settings.setValue("HiddenMessages", hidden);
                }
                // 重新加载当前会话
                int row = m_contactsList->currentRow();
                if (row >= 0) onContactSwitched(row);
            }
        }
        else if (urlStr.startsWith("download:")) {
            QString savedPath = urlStr.mid(9);
            QString fileName = m_fileNameMap.value(savedPath, "未知文件");
            if (fileName.isEmpty()) {
                QMessageBox::warning(nullptr, "提示", "文件信息缺失，无法下载。");
                return;
            }
            // 选择本地保存路径
            QString saveTo = QFileDialog::getSaveFileName(nullptr,
                "保存文件",
                QDir::homePath() + "/" + fileName,
                tr("所有文件 (*.*)"));
            if (saveTo.isEmpty()) return;
            // 向服务端发送下载请求
            QJsonObject dlReq;
            dlReq["type"] = "file_download_request";
            dlReq["saved_path"] = savedPath;
            dlReq["filename"] = fileName;
            m_tcpSocket->write(QJsonDocument(dlReq).toJson(QJsonDocument::Compact) + "\n");
        }
        else {
            // 普通链接直接交给系统打开
            QDesktopServices::openUrl(url);
        }
        });
    // 启用自定义右键菜单
    m_textBrowser->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_textBrowser, &QTextBrowser::customContextMenuRequested, this, [this](const QPoint& pos) {
        QString clickedUrl = m_textBrowser->anchorAt(pos);
        QString localFilePath;
        if (clickedUrl.startsWith("file://")) {
            localFilePath = QUrl(clickedUrl).toLocalFile();
        }
        QMenu customMenu(m_textBrowser);
        customMenu.setStyleSheet(
            "QMenu { background-color: #FFFFFF; border: 1px solid #E4E7ED; border-radius: 6px; padding: 6px 0px; box-shadow: 0px 4px 10px rgba(0,0,0,0.1); }"
            "QMenu::item { padding: 8px 12px 8px 12px; color: #303133; font-size: 14px; }"
            "QMenu::item:selected { background-color: #F2F6FC; color: #3370FF; }"
            "QMenu::icon { width: 16px; height: 16px; padding-left: 13px; }"
            "QMenu::separator { height: 1px; background: #EBEEF5; margin: 4px 12px; }"
        );
        // 统一右键菜单图标尺寸
        QString iconPrefix = "../../AttendanceClient/icon_library/Chat/";
        QIcon iconCopy(iconPrefix + "icon_copy.svg");
        QAction* actCopy = new QAction(iconCopy, "复制", &customMenu);
        customMenu.addAction(actCopy);
        QIcon iconDelete(iconPrefix + "icon_delete.svg"); 
        QAction* actDelete = new QAction(iconDelete, "删除此内容", &customMenu);
        customMenu.addAction(actDelete);
        customMenu.addSeparator();
        QIcon iconSaveImg(iconPrefix + "icon_save.svg");
        QAction* actSaveImg = new QAction(iconSaveImg, "保存文件到本地", &customMenu);
        customMenu.addAction(actSaveImg);
        QIcon iconOpenDir(iconPrefix + "icon_open.svg");
        QAction* actOpenDir = new QAction(iconOpenDir, "打开文件所在目录", &customMenu);
        customMenu.addAction(actOpenDir);
        customMenu.addSeparator();
        QIcon iconForward(iconPrefix + "icon_forward.svg");
        QAction* actForward = new QAction(iconForward, "转发给他人", &customMenu);
        customMenu.addAction(actForward);
        // 未选中文件时禁用文件相关操作
        if (localFilePath.isEmpty()) {
            actSaveImg->setEnabled(false);
            actOpenDir->setEnabled(false);
        }
        // 弹出菜单并获取用户选择
        QAction* selectedAct = customMenu.exec(m_textBrowser->mapToGlobal(pos));
        if (!selectedAct) return;
        // 复制内容到剪贴板
        if (selectedAct == actCopy) {
            if (!localFilePath.isEmpty() && QFile::exists(localFilePath)) {
                QMimeData* mimeData = new QMimeData();
                QFileInfo fi(localFilePath);
                QString suffix = fi.suffix().toLower();
                if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "bmp") {
                    mimeData->setImageData(QImage(localFilePath));
                }
                mimeData->setUrls(QList<QUrl>() << QUrl::fromLocalFile(localFilePath));
                QApplication::clipboard()->setMimeData(mimeData);

            }
            else {
                // 复制选中的文字
                m_textBrowser->copy();
            }
        }
        // 删除当前消息
        else if (selectedAct == actDelete) {
            QString hashToDel;
            QTextCursor cursor = m_textBrowser->cursorForPosition(pos);
            QTextBlock block = cursor.block();
            // 向上查找消息头部的删除链接
            for (int i = 0; i < 5 && block.isValid(); ++i) {
                for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                    QTextFragment frag = it.fragment();
                    if (frag.isValid() && frag.charFormat().isAnchor()) {
                        QString href = frag.charFormat().anchorHref();
                        if (href.startsWith("del:")) {
                            hashToDel = href.mid(4);
                            break;
                        }
                    }
                }
                if (!hashToDel.isEmpty()) break;
                block = block.previous();
            }
            // 未定位到消息时给出提示
            if (hashToDel.isEmpty()) {
                QMessageBox::warning(m_textBrowser->window(), "提示", "无法定位该消息，请在要删除的消息或文件图标上点击右键。");
                return;
            }
            // 写入隐藏列表并刷新当前会话
            int ret = QMessageBox::question(m_textBrowser->window(), "删除确认", "确定要在本地聊天记录中删除这条消息吗？", QMessageBox::Yes | QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                QSettings settings("ChatLocalSettings.ini", QSettings::IniFormat);
                QStringList hidden = settings.value("HiddenMessages").toStringList();
                if (!hidden.contains(hashToDel)) {
                    hidden.append(hashToDel);
                    settings.setValue("HiddenMessages", hidden);
                }
                // 重新加载当前会话
                int row = m_contactsList->currentRow();
                if (row >= 0) onContactSwitched(row);
            }
        
        }
        // 将文件保存到本地
        else if (selectedAct == actSaveImg) {
            if (QFile::exists(localFilePath)) {
                QFileInfo fi(localFilePath);
                // 打开另存为对话框
                QString savePath = QFileDialog::getSaveFileName(m_textBrowser->window(), "另存为", fi.fileName(), "所有文件 (*.*)");
                if (!savePath.isEmpty()) {
                    if (QFile::copy(localFilePath, savePath)) {
                        QMessageBox::information(m_textBrowser->window(), "成功", "文件已成功保存到本地！");
                    }
                    else {
                        QMessageBox::warning(m_textBrowser->window(), "失败", "保存失败，可能是权限不足或存在同名文件。");
                    }
                }
            }
            else {
                QMessageBox::warning(m_textBrowser->window(), "错误", "原文件已被清理，无法保存！");
            }
        }
        // 打开文件所在目录
        else if (selectedAct == actOpenDir) {
            if (QFile::exists(localFilePath)) {
#ifdef Q_OS_WIN
                // Windows 下高亮选中文件
                QProcess::startDetached("explorer.exe", QStringList() << "/select," << QDir::toNativeSeparators(localFilePath));
#else
                // 其他平台直接打开目录
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(localFilePath).absolutePath()));
#endif
            }
            else {
                QMessageBox::warning(m_textBrowser->window(), "错误", "该文件已被删除或转移，无法打开目录！");
            }
        }
        // 转发给其他联系人
        else if (selectedAct == actForward) {
            QString selectedText = m_textBrowser->textCursor().selectedText();
            if (localFilePath.isEmpty() && selectedText.isEmpty()) {
                QMessageBox::warning(m_textBrowser->window(), "提示", "请先选中文本，或右键点击要转发的内容！");
                return;
            }
            // 构建联系人选择窗口
            QDialog forwardDialog(m_textBrowser->window());
            forwardDialog.setWindowTitle("转发给...");
            forwardDialog.setFixedSize(320, 450);
            QVBoxLayout* flayout = new QVBoxLayout(&forwardDialog);
            QListWidget* list = new QListWidget(&forwardDialog);
            list->setStyleSheet("QListWidget { border: 1px solid #E4E7ED; border-radius: 6px; outline: none; } "
                "QListWidget::item { height: 40px; padding-left: 10px; font-size: 14px; } "
                "QListWidget::item:hover { background-color: #F5F5F5; } "
                "QListWidget::item:selected { background-color: #E3F2FD; color: #165DFF; font-weight: bold; }");
            // 镜像克隆左侧主界面的联系人列表
            for (int i = 0; i < m_contactsList->count(); ++i) {
                QListWidgetItem* origItem = m_contactsList->item(i);
                QListWidgetItem* newItem = new QListWidgetItem(origItem->icon(), origItem->text());
                newItem->setData(Qt::UserRole, origItem->data(Qt::UserRole));
                list->addItem(newItem);
            }
            flayout->addWidget(list);
            QPushButton* btnOk = new QPushButton("发送", &forwardDialog);
            btnOk->setStyleSheet("QPushButton { background-color: #3370FF; color: white; border-radius: 6px; height: 35px; font-weight: bold; }"
                "QPushButton:hover { background-color: #4E83FF; }");
            flayout->addWidget(btnOk);
            connect(btnOk, &QPushButton::clicked, &forwardDialog, &QDialog::accept);
            // 确认接收对象后执行转发
            if (forwardDialog.exec() == QDialog::Accepted) {
                QListWidgetItem* sel = list->currentItem();
                if (!sel) return;
                QString targetData = sel->data(Qt::UserRole).toString();
                bool isGroup = targetData.startsWith("GROUP_");
                QString targetName = isGroup ? targetData.mid(6) : targetData;
                // 转发文件或图片
                if (!localFilePath.isEmpty() && QFile::exists(localFilePath)) {
                    QFileInfo fi(localFilePath);
                    bool isImg = (fi.suffix().toLower() == "png" || fi.suffix().toLower() == "jpg" || fi.suffix().toLower() == "jpeg");

                    if (!isImg) {
                        // 大文件走分片发送
                        sendFileChunked(localFilePath, fi.fileName(), targetName, isGroup, isGroup ? targetName : "");
                    }
                    else {
                        // 小图片直接走 Base64
                        QFile f(localFilePath);
                        if (f.open(QIODevice::ReadOnly)) {
                            QByteArray b64 = f.readAll().toBase64();
                            f.close();
                            QJsonObject outJson;
                            outJson["from"] = m_myName;
                            outJson["msg"] = QString(b64);
                            outJson["filename"] = fi.fileName();
                            outJson["type"] = isGroup ? "group_image" : "image";
                            if (isGroup) outJson["department"] = targetName;
                            else outJson["to"] = targetName;
                            m_tcpSocket->write(QJsonDocument(outJson).toJson(QJsonDocument::Compact) + "\n");
                        }
                    }
                    QMessageBox::information(m_textBrowser->window(), "成功", "转发完成！");
                }
                // 转发选中的文字
                else if (!selectedText.isEmpty()) {
                    QJsonObject outJson;
                    outJson["from"] = m_myName;
                    outJson["msg"] = selectedText;
                    outJson["msg_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    outJson["type"] = isGroup ? "group_chat" : "chat";
                    if (isGroup) outJson["department"] = targetName;
                    else outJson["to"] = targetName;
                    m_tcpSocket->write(QJsonDocument(outJson).toJson(QJsonDocument::Compact) + "\n");
                    QMessageBox::information(m_textBrowser->window(), "成功", "转发完成！");
                }
            }
        }
        });
    // 绑定网络事件
    connect(m_tcpSocket, &QTcpSocket::connected, this, &ChatModule::onConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ChatModule::onDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ChatModule::onReadyRead);
    // 绑定界面控件事件
    connect(m_contactsList, &QListWidget::currentRowChanged, this, &ChatModule::onContactSwitched);
    connect(m_btnEmoji, &QPushButton::clicked, this, &ChatModule::onBtnEmojiClicked);
    connect(m_btnFolder, &QPushButton::clicked, this, &ChatModule::onBtnFolderClicked);
    connect(m_btnHistory, &QPushButton::clicked, this, &ChatModule::onBtnHistoryClicked);
    connect(m_btnMoreOpt, &QPushButton::clicked, this, &ChatModule::onBtnMoreOptClicked);
    m_targetLabel->setText("请在左侧选择联系人开始聊天");
    // 配置搜索框
    m_searchEdit->setPlaceholderText("搜索联系人...");
    QAction* searchAction = new QAction(m_searchEdit);
    searchAction->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_search.svg"));
    m_searchEdit->addAction(searchAction, QLineEdit::LeadingPosition);
    m_searchEdit->setStyleSheet(
        "QLineEdit { background: #F2F3F5; border: none; border-radius: 18px; padding-left: 5px; color: #1F2329; }"
        "QLineEdit:focus { background: #FFF; border: 1px solid #3370FF; }"
    );
    // 按关键字实时过滤联系人
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
    // 使用分割器重建聊天区域布局
    QWidget* mainPage = m_textBrowser->parentWidget();
    // 创建竖向分割器
    QSplitter* splitter = new QSplitter(Qt::Vertical, mainPage);
    splitter->setHandleWidth(4);
    splitter->setStyleSheet("QSplitter::handle { background-color: #DCDFE6; margin: 2px 0px; }");
    // 将原聊天窗口替换到分割器中
    if (mainPage && mainPage->layout()) {
        mainPage->layout()->replaceWidget(m_textBrowser, splitter);
    }
    // 构建底部输入区域
    QWidget* bottomWidget = new QWidget(splitter);
    bottomWidget->setMinimumHeight(120);
    QVBoxLayout* bottomLayout = new QVBoxLayout(bottomWidget);
    bottomLayout->setContentsMargins(0, 5, 0, 0);
    bottomLayout->setSpacing(5);
    // 工具按钮样式
    QString toolBtnStyle =
        "QPushButton { background: transparent; color: #4E5969; border-radius: 6px; padding: 6px 12px; font-weight: bold; border: none; font-size: 13px; }"
        "QPushButton:hover { background: #E5E6EB; color: #165DFF; }";
    // 配置表情按钮
    m_btnEmoji->setText("");
    m_btnEmoji->setCursor(Qt::PointingHandCursor);
    m_btnEmoji->setStyleSheet(toolBtnStyle);
    m_btnEmoji->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_emoji.svg"));
    m_btnEmoji->setIconSize(QSize(18, 18));
    // 配置文件按钮
    m_btnFolder->setText("");
    m_btnFolder->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_folder.svg"));
    m_btnFolder->setIconSize(QSize(18, 18));
    m_btnFolder->setCursor(Qt::PointingHandCursor);
    m_btnFolder->setStyleSheet(toolBtnStyle);
    // 配置历史记录按钮
    m_btnHistory->setText("");
    m_btnHistory->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_history.svg"));
    m_btnHistory->setIconSize(QSize(18, 18));
    m_btnHistory->setCursor(Qt::PointingHandCursor);
    m_btnHistory->setStyleSheet(toolBtnStyle);
    // 组装顶部工具栏
    QHBoxLayout* toolLayout = new QHBoxLayout();
    toolLayout->setContentsMargins(5, 5, 5, 5);
    toolLayout->setSpacing(8);
    toolLayout->addWidget(m_btnEmoji);
    toolLayout->addWidget(m_btnFolder);
    toolLayout->addWidget(m_btnHistory);
    toolLayout->addStretch();
    bottomLayout->addLayout(toolLayout);
    // 组装输入框和发送按钮
    QWidget* textEditWrapper = new QWidget(bottomWidget);
    QGridLayout* wrapperLayout = new QGridLayout(textEditWrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(0);
    // 创建多行输入框
    m_textEdit = new QTextEdit(textEditWrapper);
    m_textEdit->setPlaceholderText("在此输入消息...");
    m_textEdit->setStyleSheet(
        "QTextEdit { border: 1px solid #DCDFE6; border-radius: 6px; padding: 10px 110px 42px 10px; background: white; color: #303133; font-size: 14px; }"
        "QTextEdit:focus { border-color: #409EFF; }"
    );
    m_textEdit->setMinimumHeight(80);
    m_textEdit->installEventFilter(this);
    // 创建发送按钮
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
    // 完成分割器组装
    splitter->addWidget(m_textBrowser);
    splitter->addWidget(bottomWidget);
    splitter->setStretchFactor(0, 8);
    splitter->setStretchFactor(1, 2);
    // 清理旧输入控件
    QWidget* oldContainer = m_lineEdit;
    while (oldContainer && oldContainer->parentWidget() != mainPage && oldContainer->parentWidget() != nullptr) {
        oldContainer = oldContainer->parentWidget();
    }
    if (oldContainer && oldContainer != mainPage && oldContainer != m_textBrowser && oldContainer != splitter) {
        oldContainer->hide();
        oldContainer->deleteLater();
    }
    else {
        m_lineEdit->hide();
        m_lineEdit->deleteLater();
    }
}
// 连接服务器并加载联系人列表
void ChatModule::connectToServer(const QString& ip, quint16 port, const QString& myName) {
    m_myName = myName;
    loadContactsFromDatabase();
    m_tcpSocket->connectToHost(ip, port);
}
// 连接成功后发送登录信息
void ChatModule::onConnected() {
    QJsonObject json;
    json["type"] = "login";
    json["name"] = m_myName;
    m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
}
// 连接断开时更新提示
void ChatModule::onDisconnected() {
    m_targetLabel->setText("网络已断开");
}
// 从服务器加载联系人列表
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
    // 添加公司总群
    QListWidgetItem* allGroupItem = new QListWidgetItem(" 【公司总群】");
    allGroupItem->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_megaphone.svg"));
    allGroupItem->setData(Qt::UserRole, "GROUP_公司总群");
    m_contactsList->addItem(allGroupItem);
    // 添加部门群
    QJsonArray deptArr = res["departments"].toArray();
    for (int i = 0; i < deptArr.size(); ++i) {
        QString dept = deptArr[i].toString();
        QListWidgetItem* item = new QListWidgetItem(QString(" 【部门群】%1").arg(dept));
        item->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_department_group.svg"));
        item->setData(Qt::UserRole, "GROUP_" + dept);
        m_contactsList->addItem(item);
    }
    // 添加个人联系人并渲染头像
    QJsonArray userArr = res["users"].toArray();
    for (int i = 0; i < userArr.size(); ++i) {
        QJsonObject u = userArr[i].toObject();
        QString name = u["name"].toString();
        QString dept = u["department"].toString();
        QString role = u["role"].toString();
        QString avatarData = u["avatar"].toString();
        QString jobTitle = u["job_title"].toString();
        if (jobTitle.isEmpty() || jobTitle == "未分配") jobTitle = "员工";
        QString formattedId = QString("%1").arg(u["id"].toInt(), 3, 10, QChar('0'));
        QListWidgetItem* item = new QListWidgetItem(
            QString(" %1 [%2] (%3-%4)").arg(name, formattedId, dept, jobTitle));
        // 生成圆形头像
        QPixmap avatarPixmap;
        bool hasRealAvatar = false;
        if (!avatarData.isEmpty()) {
            QByteArray ba = QByteArray::fromBase64(avatarData.toUtf8());
            QImage rawImg;
            if (rawImg.loadFromData(ba)) {
                // 缩放并居中裁剪为正方形
                int size = qMin(rawImg.width(), rawImg.height());
                QImage squareImg = rawImg.copy((rawImg.width() - size) / 2, (rawImg.height() - size) / 2, size, size)
                    .scaled(48, 48, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                // 绘制圆形头像底图
                QImage result(48, 48, QImage::Format_ARGB32_Premultiplied);
                result.fill(Qt::transparent);
                QPainter painter(&result);
                painter.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                path.addEllipse(0, 0, 48, 48);
                painter.setClipPath(path);
                painter.drawImage(0, 0, squareImg);
                painter.end();
                avatarPixmap = QPixmap::fromImage(result);
                hasRealAvatar = true;
            }
        }
        // 有真实头像就使用头像，否则使用默认图标
        if (hasRealAvatar) {
            item->setIcon(QIcon(avatarPixmap));
        }
        else {
            if (role.contains("管理员")) {
                item->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_admin.svg"));
            }
            else {
                item->setIcon(QIcon("../../AttendanceClient/icon_library/Chat/icon_staff.svg"));
            }
        }
        item->setData(Qt::UserRole, name);
        m_contactsList->addItem(item);
    }
    // 调整列表图标尺寸
    m_contactsList->setIconSize(QSize(32, 32));
}
// 切换联系人后加载聊天历史
void ChatModule::onContactSwitched(int currentRow) {
    if (currentRow < 0) return;
    // 判断当前会话是群聊还是单聊
    QString targetData = m_contactsList->item(currentRow)->data(Qt::UserRole).toString().trimmed();
    if (targetData.startsWith("GROUP_")) {
        m_isCurrentGroup = true;
        m_currentTarget = targetData.replace("GROUP_", "").trimmed();
        m_targetLabel->setText("正在与 【群聊：" + m_currentTarget + "】 聊天");
    }
    else {
        m_isCurrentGroup = false;
        m_currentTarget = targetData;
        m_targetLabel->setText("正在与 【" + m_currentTarget + "】 聊天");
        // 单聊时发送已读回执
        if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject json;
            json["type"] = "read_receipt";
            json["from"] = m_myName;
            json["to"] = m_currentTarget;
            m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
            m_tcpSocket->flush();
        }
    }
    // 从服务器查询聊天历史
    QJsonObject histReq;
    histReq["type"] = "query_chat_history";
    histReq["me"] = m_myName;
    histReq["target"] = m_currentTarget;
    histReq["is_group"] = m_isCurrentGroup;
    QJsonObject histRes = NetworkHelper::request(histReq);
    m_chatHistories[m_currentTarget] = "";
    QSettings settings("ChatLocalSettings.ini", QSettings::IniFormat);
    QStringList hiddenHashes = settings.value("HiddenMessages").toStringList();
    // 兼容不同响应字段
    QJsonArray dataArr;
    if (histRes.contains("data")) dataArr = histRes["data"].toArray();
    else if (histRes.contains("history")) dataArr = histRes["history"].toArray();
    // 准备本地文件目录
    QString myLocalFolder = this->property("localFolder").toString();
    if (myLocalFolder.isEmpty()) myLocalFolder = "Unknown_User";
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceClient/ChatFiles/client/" + myLocalFolder;
    QString clientDirPath = QDir::cleanPath(rawPath);
    QDir dir;
    if (!dir.exists(clientDirPath)) dir.mkpath(clientDirPath);
    // 遍历历史记录并拼接显示内容
    for (int i = 0; i < dataArr.size(); ++i) {
        QJsonObject o = dataArr[i].toObject();
        // 兼容不同字段名
        QString sender = o.contains("sender") ? o["sender"].toString() : o["from"].toString();
        QString rawContent = o.contains("content") ? o["content"].toString() : o["msg"].toString();
        QString content = "";
        QString displayMsg = "";
        QString timeStr = o.contains("time") ? o["time"].toString() : o["send_time"].toString();
        QString mType = o.contains("msg_type") ? o["msg_type"].toString() : o["type"].toString();
        QString fName = o["filename"].toString();
        // 统一处理附件和文字消息
        if (mType.contains("image") || mType.contains("file") || mType == "file_meta") {
            content = "[附件占位]";
            // 获取当前用户的本地文件目录
            QString myLocalFolder = this->property("localFolder").toString();
            if (myLocalFolder.isEmpty()) myLocalFolder = "Unknown_User";
            QString clientDirPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../AttendanceClient/ChatFiles/client/" + myLocalFolder);
            QString folderUrl = QUrl::fromLocalFile(clientDirPath).toString();
            // 模糊查找本地文件
            QDir dir(clientDirPath);
            QString localFilePath = "";
            QFileInfoList list = dir.entryInfoList(QStringList() << "*" + fName, QDir::Files, QDir::Time);
            if (!list.isEmpty()) {
                localFilePath = list.first().absoluteFilePath();
            }
            // 生成附件的富文本显示
            if (mType.contains("image")) {
                if (!localFilePath.isEmpty()) {
                    // 本地存在图片时直接显示
                    QString fileUrl = QUrl::fromLocalFile(localFilePath).toString();
                    displayMsg = QString("<a href='%1'><img src='%1' width='150' style='border-radius:6px;' /></a>").arg(fileUrl, folderUrl);
                }
                else {
                    // 本地文件不存在时显示占位提示
                    displayMsg = QString("<span style='color:#909399; font-size:12px;'></span>").arg(fName, folderUrl);
                }
            }
            else {
                // 普通文件使用系统图标显示
                QFileIconProvider iconProvider;
                QFileInfo fileInfo(localFilePath.isEmpty() ? fName : localFilePath);
                QIcon icon = iconProvider.icon(fileInfo);
                if (icon.isNull()) icon = iconProvider.icon(QFileIconProvider::File);
                QPixmap pixmap = icon.pixmap(70, 70);
                if (pixmap.isNull()) { pixmap = QPixmap(70, 70); pixmap.fill(Qt::transparent); }
                QByteArray imgArray;
                QBuffer buffer(&imgArray);
                buffer.open(QIODevice::WriteOnly);
                pixmap.save(&buffer, "PNG");
                if (!localFilePath.isEmpty()) {
                    QString fileUrl = QUrl::fromLocalFile(localFilePath).toString();
                    displayMsg = QString(
                        "<a href='%2'><img src='data:image/png;base64,%1' width='70' height='70'><br>"
                        "<a href='%2' style='text-decoration:none; color:#3370FF; font-weight:bold; font-size:13px;'>%3</a>"
                    ).arg(QString(imgArray.toBase64()), fileUrl, fName, folderUrl);
                }
                else {
                    displayMsg = QString(
                        "<a href='%3'><img src='data:image/png;base64,%1' width='70' height='70'><br>"
                        "<span style='color:#909399; font-weight:bold; font-size:13px;'>%2</span><br>"
                        "<span style='font-size:12px; color:#F56C6C;'>[本地文件已清理]</span><br>"
                    ).arg(QString(imgArray.toBase64()), fName, folderUrl);
                }
            }
        }
        else {
            // 纯文字消息按需解码
            if (rawContent.startsWith("B64:")) {
                content = QString::fromUtf8(QByteArray::fromBase64(rawContent.mid(4).toUtf8()));
            }
            else {
                content = rawContent;
            }
            displayMsg = content.toHtmlEscaped();
        }
        QString myBgColor = (mType.contains("image") || mType.contains("file") || mType == "file_meta") ? "transparent" : "#95EC69";
        QString otherBgColor = (mType.contains("image") || mType.contains("file") || mType == "file_meta") ? "transparent" : "#FFFFFF";
        QString otherBorder = (otherBgColor == "transparent") ? "border:none;" : "border:1px solid #EAEAEA;";
        QString shortContent = content.length() > 50 ? content.left(50) : content;
        QString msgHash = QString(QCryptographicHash::hash((timeStr + sender + fName + shortContent).toUtf8(), QCryptographicHash::Md5).toHex());
        if (!msgHash.isEmpty() && msgHash != "0" && hiddenHashes.contains(msgHash)) continue;
        QString bubbleHtml;
        int isRead = o["is_read"].toInt();
        if (sender == m_myName) {
            QString readStatusHtml = (!m_isCurrentGroup) ? (isRead == 0 ? "&nbsp;&nbsp;<span style='color:#909399;'>(未读)</span>" : "&nbsp;&nbsp;<span style='color:#909399;'>(已读)</span>") : "";
            QString header = QString("<a href='del:%1' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-right:10px;'><img src='../../AttendanceClient/icon_library/Chat/icon_delete.svg' width='14' height='14' align='middle'> 删除</a>"
                "<span style='color:#999999; font-size:12px;'>%2 [我]</span>%3").arg(msgHash, timeStr, readStatusHtml);

            bubbleHtml = QString("<div style='text-align:right; margin-bottom:10px;'>%1<br><span style='background-color:%2; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#000; text-align:left;'>%3</span></div>").arg(header, myBgColor, displayMsg);
        }
        else {
            QString header = QString("<span style='color:#999999; font-size:12px;'>[%1] %2</span>"
                "<a href='del:%4' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-left:10px;'><img src='../../AttendanceClient/icon_library/Chat/icon_delete.svg' width='14' height='14' align='middle'> 删除</a>").arg(sender, timeStr, msgHash);

            bubbleHtml = QString("<div style='text-align:left; margin-bottom:10px;'>%1<br><span style='background-color:%2; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#000; %3'>%4</span></div>").arg(header, otherBgColor, otherBorder, displayMsg);
        }
        m_chatHistories[m_currentTarget] += bubbleHtml;
    }
    // 显示聊天历史
    if (m_chatHistories[m_currentTarget].isEmpty()) {
        QString debugInfo = QString("<div style='text-align:center; color:#AAAAAA; margin-top:10px;'>暂无聊天记录</small></div>").arg(dataArr.size());
        m_textBrowser->setHtml(debugInfo);
    }
    else {
        m_textBrowser->setHtml(m_chatHistories.value(m_currentTarget, ""));
    }
    m_textBrowser->moveCursor(QTextCursor::End);
}
// 发送文本消息并同步到界面和服务器
void ChatModule::sendMessage() {
    QString pendingFile = m_textEdit->property("pending_file").toString();
    if (!pendingFile.isEmpty() && QFile::exists(pendingFile)) {
        executeFileSend(pendingFile);
        m_textEdit->setProperty("pending_file", "");
        m_textEdit->clear();
        return;
    }
    QString msg = m_textEdit->toPlainText().trimmed();
    if (msg.isEmpty() || m_currentTarget.isEmpty()) return;
    // 统计最近使用的表情
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
    // 生成消息时间和标识
    QString timeStr = QDateTime::currentDateTime().toString("MM-dd HH:mm");
    QString msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString readStatus = m_isCurrentGroup ? "" : QString("<span style='color:#AAAAAA; font-size:12px;'> (未读)</span>");
    // 生成消息哈希用于删除定位
    QString msgHash = QString(QCryptographicHash::hash((timeStr + m_myName + msg).toUtf8(), QCryptographicHash::Md5).toHex());
    QString header = QString("<a href='del:%1' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-right:10px;'>"
        "<img src='../../AttendanceClient/icon_library/Chat/icon_delete.svg' width='14' height='14' align='middle'> 删除</a>"
        "<span style='color:#999999; font-size:12px;'>%2 [我] </span> %3").arg(msgHash, timeStr, readStatus);
    // 生成消息气泡
    QString myMsgHtml = "<div style='text-align:right; margin-bottom:10px;'>"
        + header + "<br>"
        "<span style='background-color:#95EC69; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; color:#000000;'>"
        + msg.toHtmlEscaped() + "</span></div>";
    // 更新本地显示
    m_chatHistories[m_currentTarget] += myMsgHtml;
    m_textBrowser->append(myMsgHtml);
    m_textEdit->clear();
    QTimer::singleShot(50, m_textBrowser, [this]() {
        m_textBrowser->moveCursor(QTextCursor::End);
        m_textBrowser->verticalScrollBar()->setValue(m_textBrowser->verticalScrollBar()->maximum());
        });
    // 发送消息到服务器
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
// 选择文件并发送
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
    if (file.size() > 50 * 1024 * 1024) {
        QMessageBox::warning(nullptr, "超限", "为保证网络传输稳定，单次限制发送 50MB 以内的文件！\n如需传输大文件，请使用网盘或共享目录。");
        return;
    }
    // 确保本地缓存目录存在
    QString myLocalFolder = this->property("localFolder").toString();
    if (myLocalFolder.isEmpty()) myLocalFolder = "Unknown_User";
    QString clientDirPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../AttendanceClient/ChatFiles/client/" + myLocalFolder);
    QDir dir;
    if (!dir.exists(clientDirPath)) {
        dir.mkpath(clientDirPath);
    }
    // 复制文件到本地缓存
    QString localFileName = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + fi.fileName();
    QString copiedFilePath = clientDirPath + "/" + localFileName;
    QFile::copy(filePath, copiedFilePath);
    qint64 fileSize = file.size();
    // 非图片文件统一走分片发送
    if (!isImage) {
        sendFileChunked(filePath, fi.fileName(), m_currentTarget,
            m_isCurrentGroup, m_isCurrentGroup ? m_currentTarget : "");
        QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
        QString fileUrl = QUrl::fromLocalFile(copiedFilePath).toString();
        QFileIconProvider iconProvider;
        QIcon icon = iconProvider.icon(fi);
        if (icon.isNull()) icon = iconProvider.icon(QFileIconProvider::File);
        QPixmap pixmap = icon.pixmap(70, 70);
        if (pixmap.isNull()) {
            pixmap = QPixmap(70, 70);
            pixmap.fill(Qt::transparent);
        }
        QByteArray imgArray;
        QBuffer buffer(&imgArray);
        buffer.open(QIODevice::WriteOnly);
        pixmap.save(&buffer, "PNG");
        QString displayHtml = QString(
            "<div style='text-align:right; margin-bottom:10px;'>"
            "<span style='color:#999999; font-size:12px;'>%1 [我]</span><br>"
            "<span style='display:inline-block; background:transparent; border-radius:6px; padding:10px; color:#000000; text-align:left;'>"
            "<a href='%3'><img src='data:image/png;base64,%2' width='70' height='70'><br>"
            "<a href='%3' style='text-decoration:none; color:#3370FF; font-weight:bold; font-size:13px;'>%4 (%5 MB)</a><br>"
            "</span></div>"
        ).arg(timeStr, QString(imgArray.toBase64()), fileUrl, fi.fileName()).arg(fileSize / 1048576.0, 0, 'f', 1);
        m_chatHistories[m_currentTarget] += displayHtml;
        m_textBrowser->append(displayHtml);
        QTimer::singleShot(50, m_textBrowser, [this]() {
            m_textBrowser->moveCursor(QTextCursor::End);
            m_textBrowser->verticalScrollBar()->setValue(m_textBrowser->verticalScrollBar()->maximum());
            });
        return;
    }
    // 图片和小文件走 Base64 发送
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray fileData = file.readAll();
        file.close();
        QString base64Data;
        if (isImage) {
            QImage img(filePath);
            img = img.scaled(400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QByteArray imgArray;
            QBuffer buffer(&imgArray);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, suffix.toUpper().toLatin1());
            base64Data = QString(imgArray.toBase64());
        }
        else {
            base64Data = QString(fileData.toBase64());
        }
        QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
        QString displayContent;
        QString fileUrl = QUrl::fromLocalFile(copiedFilePath).toString();

        if (isImage) {
            displayContent = QString("<a href='%1'><img src='%1' width='150' style='border-radius:6px;' />").arg(fileUrl);
        }
        else {
            displayContent = QString("<a href='%1' style='text-decoration:none; color:#3370FF;'>").arg(fileUrl, fi.fileName());
        }
        QString msgHash = QString(QCryptographicHash::hash((timeStr + m_myName + base64Data).toUtf8(), QCryptographicHash::Md5).toHex());
        QString header = QString("<a href='del:%1' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-right:10px;'>[删除]</a>"
            "<span style='color:#999999; font-size:12px;'>%2 [我] </span>").arg(msgHash, timeStr);
        QString myMsgHtml = "<div style='text-align:right; margin-bottom:10px;'>" + header + "<br><span style='background-color:#95EC69; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px;'>" + displayContent + "</span></div>";
        m_chatHistories[m_currentTarget] += myMsgHtml;
        m_textBrowser->append(myMsgHtml);
        QTimer::singleShot(50, m_textBrowser, [this]() {
            m_textBrowser->moveCursor(QTextCursor::End);
            m_textBrowser->verticalScrollBar()->setValue(m_textBrowser->verticalScrollBar()->maximum());
            });
        if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject outJson;
            outJson["from"] = m_myName;
            outJson["msg"] = base64Data;
            outJson["filename"] = fi.fileName();
            if (m_isCurrentGroup) {
                outJson["type"] = isImage ? "group_image" : "group_file";
                outJson["department"] = m_currentTarget;
            }
            else {
                outJson["type"] = isImage ? "image" : "file";
                outJson["to"] = m_currentTarget;
            }
            m_tcpSocket->write(QJsonDocument(outJson).toJson(QJsonDocument::Compact) + "\n");
            m_tcpSocket->flush();
        }
    }
}
// 按分片发送大文件，避免单包过大
void ChatModule::sendFileChunked(const QString& filePath, const QString& fileName, const QString& target, bool isGroup, const QString& department)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    // 使用较小分片发送，降低单包体积
    const qint64 CHUNK_SIZE = 1024 * 256;
    int totalChunks = (file.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int chunkIndex = 0;
    while (!file.atEnd()) {
        QByteArray chunk = file.read(CHUNK_SIZE);
        QJsonObject json;
        json["type"] = isGroup ? "group_file_chunk" : "file_chunk";
        json["from"] = m_myName;
        if (isGroup) json["department"] = department;
        else json["to"] = target;
        json["filename"] = fileName;
        json["chunk_index"] = chunkIndex++;
        json["total_chunks"] = totalChunks;
        json["file_data"] = QString(chunk.toBase64());
        QByteArray outData = QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n";
        // 反复写入，确保分片发完整
        const char* dataPtr = outData.constData();
        int bytesLeft = outData.size();
        while (bytesLeft > 0) {
            int written = m_tcpSocket->write(dataPtr, bytesLeft);
            if (written < 0) break;
            bytesLeft -= written;
            dataPtr += written;
            m_tcpSocket->waitForBytesWritten(10);
        }
        QCoreApplication::processEvents();
    }
    file.close();
}
// 处理服务器推送的消息
void ChatModule::onReadyRead() {
    while (m_tcpSocket->canReadLine()) {
        QByteArray data = m_tcpSocket->readLine();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) continue;
        QJsonObject json = doc.object();
        QString type = json["type"].toString();
        QString fromUser = json["from"].toString();
        // 处理广播消息
        if (type == "broadcast") {
            QString content = json["msg"].toString();
            emit broadcastReceived(fromUser, content);
            QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
            QString receiveHtml = "<div style='text-align:center; margin-bottom:10px;'>"
                "<span style='background-color:#F56C6C; color:#FFF; padding:6px 12px; border-radius:6px; font-size:12px;'>"
                "<b>全局系统广播</b> [" + fromUser + "] " + timeStr + "<br><br>" + content.toHtmlEscaped() + "</span></div>";
            m_chatHistories["公司总群"] += receiveHtml;
            if (m_currentTarget == "公司总群") {
                m_textBrowser->append(receiveHtml);
                QTimer::singleShot(50, m_textBrowser, [this]() {
                    m_textBrowser->moveCursor(QTextCursor::End);
                    m_textBrowser->verticalScrollBar()->setValue(m_textBrowser->verticalScrollBar()->maximum());
                    });
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
        // 处理文件分片并拼接到本地
        if (type == "file_chunk" || type == "group_file_chunk") {
            QString filename = json["filename"].toString();
            int chunkIndex = json["chunk_index"].toInt();
            int totalChunks = json["total_chunks"].toInt();
            QByteArray chunkData = QByteArray::fromBase64(json["file_data"].toString().toUtf8());
            QString myLocalFolder = this->property("localFolder").toString();
            if (myLocalFolder.isEmpty()) myLocalFolder = "Unknown_User";
            QString clientDirPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../AttendanceClient/ChatFiles/client/" + myLocalFolder);
            QDir dir; if (!dir.exists(clientDirPath)) dir.mkpath(clientDirPath);
            QString localFilePath = clientDirPath + "/recv_" + filename;
            QFile file(localFilePath);
            // 首个分片时清理旧文件
            if (chunkIndex == 0 && file.exists()) file.remove();
            // 追加写入磁盘
            if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
                file.write(chunkData);
                file.close();
            }
            // 最后一片到达后显示文件消息
            if (chunkIndex == totalChunks - 1) {
                QString targetTab = type.contains("group") ? json["department"].toString() : fromUser;
                QString fileUrl = QUrl::fromLocalFile(localFilePath).toString();
                QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
                QFileIconProvider iconProvider;
                QFileInfo fileInfo(localFilePath);
                QIcon icon = iconProvider.icon(fileInfo);
                if (icon.isNull()) icon = iconProvider.icon(QFileIconProvider::File);
                QPixmap pixmap = icon.pixmap(70, 70);
                if (pixmap.isNull()) { pixmap = QPixmap(70, 70); pixmap.fill(Qt::transparent); }
                QByteArray imgArray;
                QBuffer buffer(&imgArray);
                buffer.open(QIODevice::WriteOnly);
                pixmap.save(&buffer, "PNG");
                // 生成文件消息的富文本内容
                QString htmlContent = QString(
                    "<a href='%2'><img src='data:image/png;base64,%1' width='70' height='70'><br>"
                    "<a href='%2' style='text-decoration:none; color:#3370FF; font-weight:bold; font-size:13px;'>%3</a><br>"
                    "<span style='font-size:12px; color:#999999;'>(接收完成，点击打开)</span>"
                ).arg(QString(imgArray.toBase64()), fileUrl, filename);
                QString receiveHtml = "<div style='text-align:left; margin-bottom:10px;'>"
                    "<span style='color:#999999; font-size:12px;'>[" + fromUser + "] " + timeStr + "</span><br>"
                    "<span style='background-color:transparent; padding:8px 12px; display:inline-block; margin-top:4px; font-size:14px; color:#000000;'>"
                    + htmlContent + "</span></div>";
                m_chatHistories[targetTab] += receiveHtml;
                if (m_currentTarget == targetTab) {
                    m_textBrowser->append(receiveHtml);
                    QTimer::singleShot(50, m_textBrowser, [this]() {
                        m_textBrowser->moveCursor(QTextCursor::End);
                        m_textBrowser->verticalScrollBar()->setValue(m_textBrowser->verticalScrollBar()->maximum());
                        });
                }
            }
            continue;
        }
        // 确定消息所属会话
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
            QString clientDirPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../AttendanceClient/ChatFiles/client/" + myLocalFolder);
            QDir dir;
            if (!dir.exists(clientDirPath)) dir.mkpath(clientDirPath);
            QString localFileName = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + originalFileName;
            QString localFilePath = clientDirPath + "/" + localFileName;
            QFile localFile(localFilePath);
            if (localFile.open(QIODevice::WriteOnly)) {
                localFile.write(QByteArray::fromBase64(content.toUtf8()));
                localFile.close();
            }
            QString fileUrl = QUrl::fromLocalFile(localFilePath).toString();
            if (type.contains("image")) {
                htmlContent = QString("<a href='%1'><img src='%1' width='150' style='border-radius:6px;' /></a><br><a href='%1' style='font-size:12px;color:gray;text-decoration:none;'>(点击外部查看原图)</a>").arg(fileUrl);
            }
            else {
                htmlContent = QString("<a href='%1' style='text-decoration:none; color:#3370FF;'>收到小文件: %2<br><span style='font-size:12px;'>(点击打开)</span></a>").arg(fileUrl, originalFileName);
            }
        }
        QString msgHash = QString(QCryptographicHash::hash((timeStr + fromUser + content).toUtf8(), QCryptographicHash::Md5).toHex());
        QString header = QString("<span style='color:#999999; font-size:12px;'>[%1] %2 %3</span>"
            "<a href='del:%4' style='color:#F56C6C; text-decoration:none; font-size:12px; margin-left:10px;'>[删除]</a>").arg(fromUser, timeStr, offlineTag, msgHash);
        QString receiveHtml = "<div style='text-align:left; margin-bottom:10px;'>" + header + "<br><span style='background-color:#FFFFFF; padding:8px 12px; border-radius:6px; display:inline-block; margin-top:4px; font-size:14px; border:1px solid #EAEAEA; color:#000000;'>" + htmlContent + "</span></div>";
        m_chatHistories[targetTab] += receiveHtml;
        if (m_currentTarget == targetTab) {
            m_textBrowser->append(receiveHtml);
            QTimer::singleShot(50, m_textBrowser, [this]() {
                m_textBrowser->moveCursor(QTextCursor::End);
                m_textBrowser->verticalScrollBar()->setValue(m_textBrowser->verticalScrollBar()->maximum());
                });
            if (type == "chat" || type == "image") {
                QJsonObject ack;
                ack["type"] = "read_receipt";
                ack["from"] = m_myName;
                ack["to"] = fromUser;
                m_tcpSocket->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + "\n");
                m_tcpSocket->flush();
            }
        }
    }
}
// 显示表情选择菜单
void ChatModule::onBtnEmojiClicked() {
    QMenu* emojiMenu = new QMenu(m_textEdit);
    emojiMenu->setStyleSheet(
        "QMenu { background-color: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 8px; padding: 4px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }"
    );
    // 创建表情网格布局
    QWidget* gridWidget = new QWidget(emojiMenu);
    QGridLayout* gridLayout = new QGridLayout(gridWidget);
    gridLayout->setSpacing(2);
    gridLayout->setContentsMargins(4, 4, 4, 4);
    // 表情列表
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
    // 按八列排列表情
    int row = 0, col = 0;
    for (const QString& em : dynamicEmojis) {
        QPushButton* btn = new QPushButton(em, gridWidget);
        btn->setFixedSize(38, 38);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { font-size: 22px; border: none; background: transparent; border-radius: 8px; }"
            "QPushButton:hover { background-color: #F2F3F5; }"
        );
        // 点击后插入表情
        connect(btn, &QPushButton::clicked, this, [=]() {
            m_textEdit->insertPlainText(em);
            emojiMenu->close();
            });
        gridLayout->addWidget(btn, row, col);
        col++;
        if (col >= 8) {
            col = 0;
            row++;
        }
    }
    // 将网格作为菜单内容
    QWidgetAction* widgetAction = new QWidgetAction(emojiMenu);
    widgetAction->setDefaultWidget(gridWidget);
    emojiMenu->addAction(widgetAction);
    // 计算菜单弹出位置
    QPoint btnPos = m_btnEmoji->mapToGlobal(QPoint(0, 0));
    int menuWidth = gridLayout->columnCount() * 40 + 8;
    int menuHeight = gridLayout->rowCount() * 40 + 8;
    QPoint popupPos(
        btnPos.x() - menuWidth / 2 + m_btnEmoji->width() / 2,
        btnPos.y() - menuHeight - 10
    );
    // 显示菜单
    emojiMenu->exec(popupPos);
    delete emojiMenu;
}
// 显示最近使用的表情
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
// 显示更多功能菜单
void ChatModule::onBtnMoreOptClicked() {
    if (m_currentTarget.isEmpty()) {
        QMessageBox::warning(nullptr, "提示", "请先选择聊天对象！");
        return;
    }
    QDialog dialog(nullptr);
    dialog.setMinimumSize(320, 450);
    // 群聊时显示成员列表
    if (m_isCurrentGroup) {
        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QListWidget* listWidget = new QListWidget(&dialog);
        listWidget->setStyleSheet("QListWidget {""   background-color: #FFFFFF;" "   border: 1px solid #E0E0E0;""   border-radius: 8px;""   outline: none;""}"
            "QListWidget::item {""   height: 40px;""   color: #333333;" "   font-size: 14px;" "   padding-left: 12px;""   border-bottom: 1px solid #F0F0F0;" "}"
            "QListWidget::item:hover {""   background-color: #F5F5F5;" "}"
            "QListWidget::item:selected {""   background-color: #E3F2FD;" "   color: #1565C0;" "}");
        layout->addWidget(listWidget);
        // 查询群成员
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
                item->setData(Qt::UserRole, name);
                listWidget->addItem(item);
                count++;
            }
        }
        dialog.setWindowTitle(QString("群成员列表 - %1 (%2人)").arg(m_currentTarget).arg(count));
        dialog.setStyleSheet("QDialog {""   background-color: #FFFFFF;""   border-radius: 10px;""}"
        );
        // 点击成员查看详情
        connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            QString userName = item->data(Qt::UserRole).toString();
            if (!userName.isEmpty()) {
                showUserInfo(userName);
            }
            });
        // 关闭按钮
        QPushButton* closeBtn = new QPushButton("关闭", &dialog);
        closeBtn->setMinimumHeight(35);
        layout->addWidget(closeBtn);
        connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    }
    else {
        // 单聊时直接显示个人信息
        showUserInfo(m_currentTarget);
    }
}
// 发送系统消息
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
// 发送全局广播消息
void ChatModule::sendBroadcast(const QString& msg) {
    if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject json;
        json["type"] = "broadcast";
        json["from"] = m_myName;
        json["msg"] = msg;
        m_tcpSocket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
    }
}
// 显示用户个人信息卡片
void ChatModule::showUserInfo(const QString& userName)
{
    QDialog dialog(nullptr);
    dialog.setWindowTitle("个人资料名片");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(20);
    layout->setContentsMargins(30, 30, 30, 30);
    // 头像区域
    QLabel* avatarLabel = new QLabel(&dialog);
    avatarLabel->setAlignment(Qt::AlignCenter);
    avatarLabel->setFixedSize(100, 100);
    avatarLabel->setStyleSheet("background-color: #EAEAEA; border-radius: 50px; color: #666666;");
    // 信息表单
    QFormLayout* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
    form->setHorizontalSpacing(20);
    form->setVerticalSpacing(15);
    // 查询用户资料
    QJsonObject req;
    req["type"] = "query_user_profile";
    req["name"] = userName;
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        QString d = res["department"].toString();
        QString j = res["job_title"].toString();
        QString g = res["gender"].toString();
        QString p = res["phone"].toString();
        QString avatarBase64 = res["avatar_base64"].toString();
        if (avatarBase64.isEmpty()) avatarBase64 = res["avatar_data"].toString();
        // 尝试从多个字段获取头像
        if (avatarBase64.isEmpty()) {
            QString avatarPath = res["avatar_path"].toString();
            if (avatarPath.isEmpty()) avatarPath = res["avatar"].toString();
            if (!avatarPath.isEmpty()) {
                // 远程获取头像文件
                QJsonObject avaReq;
                avaReq["type"] = "query_avatar_file";
                avaReq["avatar_path"] = avatarPath;
                QJsonObject avaRes = NetworkHelper::request(avaReq);
                if (avaRes["status"].toString() == "success") {
                    avatarBase64 = avaRes["avatar_data"].toString();
                }
            }
        }
        // 加载头像或显示首字母
        bool isAvatarLoaded = false;
        if (!avatarBase64.isEmpty()) {
            QImage img;
            if (img.loadFromData(QByteArray::fromBase64(avatarBase64.toUtf8()))) {
                // 缩放并裁剪为圆形
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
                isAvatarLoaded = true;
            }
        }
        // 头像失败时显示首字母
        if (!isAvatarLoaded) {
            avatarLabel->setText(userName.left(1));
            avatarLabel->setFont(QFont("Microsoft YaHei", 36, QFont::Bold));
        }
        // 填充资料内容
        form->addRow("姓名:", new QLabel(QString("<b>%1</b>").arg(userName), &dialog));
        form->addRow("部门:", new QLabel(d, &dialog));
        form->addRow("职务:", new QLabel(j.isEmpty() ? "未分配" : j, &dialog));
        form->addRow("性别:", new QLabel(g.isEmpty() ? "未知" : g, &dialog));
        form->addRow("电话:", new QLabel(p.isEmpty() ? "未设置" : p, &dialog));
    }
    else {
        // 查询失败时显示占位
        avatarLabel->setText("?");
        avatarLabel->setFont(QFont("Microsoft YaHei", 36, QFont::Bold));
        form->addRow(new QLabel("无法获取该用户信息", &dialog));
    }
    layout->addWidget(avatarLabel, 0, Qt::AlignHCenter);
    layout->addLayout(form);
    layout->addStretch();
    // 关闭按钮
    QPushButton* closeBtn = new QPushButton("关闭名片", &dialog);
    closeBtn->setMinimumHeight(40);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet("QPushButton { background-color: #3370FF; color: white; border-radius: 6px; font-weight: bold; font-size: 14px; } QPushButton:hover { background-color: #4E83FF; }");
    layout->addWidget(closeBtn);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}
// 处理输入框键盘事件
bool ChatModule::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_textEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->matches(QKeySequence::Paste)) {
            const QMimeData* mimeData = QApplication::clipboard()->mimeData();
            // 剪贴板包含文件路径时
            if (mimeData->hasUrls()) {
                QList<QUrl> urlList = mimeData->urls();
                if (!urlList.isEmpty()) {
                    QString localPath = urlList.first().toLocalFile();
                    if (!localPath.isEmpty() && QFile::exists(localPath)) {
                        QFileInfo fi(localPath);
                        QString suffix = fi.suffix().toLower();
                        // 图片直接显示预览
                        if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "bmp") {
                            // 在输入框中显示图片预览
                            m_textEdit->insertHtml(QString("<img src='%1' width='100' height='100'>").arg(QUrl::fromLocalFile(localPath).toString()));
                        }
                        else {
                            // 普通文件显示文件名占位
                            m_textEdit->insertPlainText(QString("[%1]").arg(fi.fileName()));
                        }
                        // 保存待发送文件路径
                        m_textEdit->setProperty("pending_file", localPath);
                        return true;
                    }
                }
            }
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            // Shift+Enter换行
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                return false;
            }
            // 单独Enter发送消息
            else {
                sendMessage();
                return true;
            }
        }
    }
    return QObject::eventFilter(obj, event);
}
// 发送已选择的文件
void ChatModule::executeFileSend(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();
    bool isImage = (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "bmp");
    // 确保本地缓存目录存在
    QString myLocalFolder = this->property("localFolder").toString();
    if (myLocalFolder.isEmpty()) myLocalFolder = "Unknown_User";
    QString clientDirPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../AttendanceClient/ChatFiles/client/" + myLocalFolder);
    QDir().mkpath(clientDirPath);
    // 复制到本地缓存
    QString localFileName = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + fi.fileName();
    QString copiedFilePath = clientDirPath + "/" + localFileName;
    QFile::copy(filePath, copiedFilePath);
    QString fileUrl = QUrl::fromLocalFile(copiedFilePath).toString();
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    // 图片和文件分别处理
    if (isImage) {
        QImage img(filePath);
        // 限制图片尺寸
        if (img.width() > 400 || img.height() > 400) {
            img = img.scaled(400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QByteArray imgArray;
        QBuffer buffer(&imgArray);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, suffix.toUpper().toLatin1());
        QString base64Data = QString(imgArray.toBase64());
        // 本地显示图片预览
        QString myMsgHtml = QString(
            "<div style='text-align:right; margin-bottom:10px;'>"
            "<span style='color:#999999; font-size:12px;'>%1 [我]</span><br>"
            "<span style='display:inline-block; background:transparent; padding:5px; margin-top:4px;'>"
            "<a href='%2'><img src='%2' width='150' style='border-radius:6px;' /></a>"
            "</span></div>"
        ).arg(timeStr, fileUrl);
        m_chatHistories[m_currentTarget] += myMsgHtml;
        m_textBrowser->append(myMsgHtml);
        // 发送到服务器
        if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject outJson;
            outJson["from"] = m_myName;
            outJson["msg"] = base64Data;
            outJson["filename"] = fi.fileName();
            outJson["type"] = m_isCurrentGroup ? "group_image" : "image";
            if (m_isCurrentGroup) outJson["department"] = m_currentTarget;
            else outJson["to"] = m_currentTarget;

            m_tcpSocket->write(QJsonDocument(outJson).toJson(QJsonDocument::Compact) + "\n");
            m_tcpSocket->flush();
        }
    }
    else {
        // 大文件走分片发送
        sendFileChunked(filePath, fi.fileName(), m_currentTarget,
            m_isCurrentGroup, m_isCurrentGroup ? m_currentTarget : "");
        // 使用文件图标显示消息
        QFileIconProvider iconProvider;
        QIcon icon = iconProvider.icon(fi);
        QPixmap pixmap = icon.pixmap(70, 70);
        QByteArray imgArray;
        QBuffer buffer(&imgArray);
        buffer.open(QIODevice::WriteOnly);
        pixmap.save(&buffer, "PNG");
        QString iconHtml = QString("<img src='data:image/png;base64,%1' width='70' height='70'>").arg(QString(imgArray.toBase64()));
        QString displayHtml = QString(
            "<div style='text-align:right; margin-bottom:10px;'>"
            "<span style='color:#999999; font-size:12px;'>%1 [我]</span><br>"
            "<span style='display:inline-block; background:transparent; padding:10px; text-align:left;'>"
            "<a href='%2'>%3</a><br>"
            "<a href='%2' style='text-decoration:none; color:#3370FF; font-weight:bold; font-size:13px;'>%4</a>"
            "</span></div>"
        ).arg(timeStr, fileUrl, iconHtml, fi.fileName());
        m_chatHistories[m_currentTarget] += displayHtml;
        m_textBrowser->append(displayHtml);
    }
    // 滚动到底部
    QTimer::singleShot(50, m_textBrowser, [this]() {
        m_textBrowser->verticalScrollBar()->setValue(m_textBrowser->verticalScrollBar()->maximum());
        });
}
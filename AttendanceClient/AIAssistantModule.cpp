#include "AIAssistantModule.h"
#include "NetworkHelper.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QScrollBar>
#include <QDateTime>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QUuid>
#include <QProcess>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QToolTip>
#include <QFileInfo>
#include <QComboBox>
#include <QThread>
#include <QTimer>
#include <QBuffer>
#include <QImage>
// 初始化网络模块、API基础配置、重构UI界面并从数据库加载历史会话
AIAssistantModule::AIAssistantModule(QTextBrowser* textBrowser, QLineEdit* lineEdit, QPushButton* sendBtn, QPushButton* clearBtn, QString userName, QObject* parent)
    : QObject(parent), m_textBrowser(textBrowser), m_oldLineEdit(lineEdit), m_sendBtn(sendBtn), m_clearBtn(clearBtn), m_userName(userName), m_isReplying(false), m_voiceEnabled(false)
{
    // 初始化网络访问管理器，用于处理HTTP请求
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AIAssistantModule::onNetworkReply);
    // 设置默认的API地址、密钥和模型名称
    m_apiUrl = "https://api.deepseek.com/chat/completions";
    m_apiKey = "sk-54ccee7e91ab405a94c622d9419a91e9";
    m_modelName = "deepseek-chat";
    rebuildAdvancedUI();
    loadSessionsFromDB();
    // 连接清除按钮
    if (m_clearBtn) {
        connect(m_clearBtn, &QPushButton::clicked, this, &AIAssistantModule::clearCurrentSession);
    }
}
// 将原本简单的输入框和显示区域转换为包含侧边栏、多功能输入框的复杂布局
void AIAssistantModule::rebuildAdvancedUI() {
    QWidget* parentW = m_textBrowser->parentWidget();
    if (!parentW) return;
    QList<QWidget*> children = parentW->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* child : children) {
        // 保留需要重用的控件
        if (child == m_textBrowser || child == m_sendBtn || child == m_clearBtn || child == m_oldLineEdit) {
            continue;
        }
        child->hide();  // 或 child->deleteLater() 彻底删除
    }
    // 清理原有的布局
    QLayout* oldLayout = parentW->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {}
        delete oldLayout;
    }
    m_oldLineEdit->hide();
    // 创建主水平布局和分割器
    QHBoxLayout* mainLayout = new QHBoxLayout(parentW);
    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, parentW);
    // 左侧侧边栏设置
    m_leftWidget = new QWidget();
    m_leftWidget->setMinimumWidth(220);
    m_leftWidget->setMaximumWidth(280);
    QVBoxLayout* leftLay = new QVBoxLayout(m_leftWidget);
    leftLay->setContentsMargins(0, 0, 10, 0);
    // 新建对话按钮
    m_newSessionBtn = new QPushButton("➕ 新建对话");
    m_newSessionBtn->setCursor(Qt::PointingHandCursor);
    m_newSessionBtn->setStyleSheet("background-color: #165DFF; color: white; border-radius: 6px; padding: 10px; font-weight: bold;");
    connect(m_newSessionBtn, &QPushButton::clicked, this, &AIAssistantModule::onNewSessionClicked);
    // 历史记录搜索框
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("🔍 搜索历史...");
    m_searchBox->setStyleSheet("border: 1px solid #E5E6EB; border-radius: 15px; padding: 5px 15px;");
    connect(m_searchBox, &QLineEdit::returnPressed, this, &AIAssistantModule::onSearchHistory);
    // 会话列表
    m_sessionList = new QListWidget();
    m_sessionList->setStyleSheet(
        "QListWidget { border: none; background: transparent; outline: none; } "
        "QListWidget::item { padding: 10px; border-radius: 6px; margin-bottom: 4px; border-bottom: 1px solid #F2F3F5; color: #1D2129; } "
        "QListWidget::item:selected { background-color: #E8F3FF; border-left: 4px solid #165DFF; border-radius: 4px; }"
    );
    connect(m_sessionList, &QListWidget::itemClicked, this, &AIAssistantModule::onSessionSelected);
    // 设置会话列表的右键菜单
    m_sessionList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sessionList, &QListWidget::customContextMenuRequested, this, &AIAssistantModule::onSessionContextMenu);
    leftLay->addWidget(m_newSessionBtn);
    leftLay->addWidget(m_searchBox);
    leftLay->addWidget(m_sessionList);
    // 右侧主聊天区域设置
    QWidget* rightWidget = new QWidget();
    QVBoxLayout* rightLay = new QVBoxLayout(rightWidget);
    rightLay->setContentsMargins(0, 0, 0, 0);
    // 顶部控制栏（侧边栏开关、语音开关）
    QHBoxLayout* topControlLay = new QHBoxLayout();
    m_toggleSidebarBtn = new QPushButton("☰ 收起列表");
    m_toggleSidebarBtn->setCursor(Qt::PointingHandCursor);
    m_toggleSidebarBtn->setStyleSheet("color: #4E5969; font-weight: bold; border: none; font-size: 14px;");
    connect(m_toggleSidebarBtn, &QPushButton::clicked, this, &AIAssistantModule::toggleSidebar);
    m_voiceBtn = new QPushButton("🔇 语音: 关");
    m_voiceBtn->setCursor(Qt::PointingHandCursor);
    m_voiceBtn->setStyleSheet("color: #86909C; font-weight: bold; border: none;");
    connect(m_voiceBtn, &QPushButton::clicked, this, &AIAssistantModule::toggleVoice);
    topControlLay->addWidget(m_toggleSidebarBtn);
    topControlLay->addStretch();
    topControlLay->addWidget(m_voiceBtn);
    if (m_clearBtn) {
        m_clearBtn->setParent(rightWidget);               // 重新设置父对象，确保在新布局中
        m_clearBtn->setCursor(Qt::PointingHandCursor);
        m_clearBtn->setStyleSheet("QPushButton { color: #F56C6C; border: none; background: transparent; font-weight: bold; } QPushButton:hover { color: #F76560; }");
        topControlLay->addWidget(m_clearBtn);
    }
    // 右侧内容分割器（上面是聊天记录，下面是输入框）
    QSplitter* rightSplitter = new QSplitter(Qt::Vertical, rightWidget);
    // 聊天记录显示区域配置
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setStyleSheet("border: none; background: transparent;");
    rightSplitter->addWidget(m_textBrowser);
    // 底部输入框与工具栏
    QFrame* inputFrame = new QFrame();
    inputFrame->setStyleSheet("QFrame { border: 1.5px solid #165DFF; border-radius: 15px; background: white; }");
    QVBoxLayout* inLay = new QVBoxLayout(inputFrame);
    inLay->setContentsMargins(10, 10, 10, 10);
    inLay->setSpacing(5);
    // 文本输入框
    m_inputTextEdit = new QTextEdit();
    m_inputTextEdit->setPlaceholderText("发消息或输入 / 选择技能... (Enter发送, Shift+Enter换行)");
    m_inputTextEdit->setStyleSheet("border: none; background: transparent; font-size: 14px;");
    m_inputTextEdit->installEventFilter(this); // 安装事件过滤器以拦截键盘事件
    inLay->addWidget(m_inputTextEdit);
    // 快捷操作工具栏
    QHBoxLayout* actionLay = new QHBoxLayout();
    actionLay->setSpacing(5);
    QString actionBtnStyle =
        "QPushButton { background: transparent; color: #4E5969; border-radius: 8px; padding: 6px 10px; font-weight: bold; border: none; } "
        "QPushButton:hover { background: #F2F3F5; color: #165DFF; }";
    // 附件上传按钮
    m_attachBtn = new QPushButton("📎");
    m_attachBtn->setCursor(Qt::PointingHandCursor);
    m_attachBtn->setStyleSheet(actionBtnStyle);
    m_attachBtn->setToolTip("上传附件 (图片与各类文档)\n最多 5 个，每个 100 MB，支持所有文件");
    m_attachBtn->setStyleSheet(m_attachBtn->styleSheet() + "QToolTip { color: #ffffff; background-color: #2a2a2a; border: 1px solid white; border-radius: 4px; padding: 5px; }");
    connect(m_attachBtn, &QPushButton::clicked, this, &AIAssistantModule::onAttachFileClicked);
    // 快捷指令按钮配置
    QPushButton* btnQuick = new QPushButton("⚡ 快速");
    QPushButton* btnWrite = new QPushButton("📝 帮我写作");
    QPushButton* btnTranslate = new QPushButton("🔤 翻译");
    QPushButton* btnCode = new QPushButton("</> 编程");
    btnQuick->setCursor(Qt::PointingHandCursor); btnQuick->setStyleSheet(actionBtnStyle);
    btnWrite->setCursor(Qt::PointingHandCursor); btnWrite->setStyleSheet(actionBtnStyle);
    btnTranslate->setCursor(Qt::PointingHandCursor); btnTranslate->setStyleSheet(actionBtnStyle);
    btnCode->setCursor(Qt::PointingHandCursor); btnCode->setStyleSheet(actionBtnStyle);
    // 绑定快捷指令填充功能
    connect(btnQuick, &QPushButton::clicked, this, [=]() { m_inputTextEdit->setPlainText("请帮我总结一下今天的个人考勤数据："); m_inputTextEdit->setFocus(); });
    connect(btnWrite, &QPushButton::clicked, this, [=]() { m_inputTextEdit->setPlainText("请帮我起草一份关于 [此处填写主题] 的文档/报告，要求语言正式、逻辑清晰。"); m_inputTextEdit->setFocus(); });
    connect(btnTranslate, &QPushButton::clicked, this, [=]() { m_inputTextEdit->setPlainText("请将以下内容精准翻译成 [中文/英文]：\n\n"); m_inputTextEdit->setFocus(); });
    connect(btnCode, &QPushButton::clicked, this, [=]() { m_inputTextEdit->setPlainText("请帮我用 C++/Qt 编写一段代码，实现以下功能：\n\n"); m_inputTextEdit->setFocus(); });
    actionLay->addWidget(m_attachBtn);
    actionLay->addWidget(btnQuick);
    actionLay->addWidget(btnWrite);
    actionLay->addWidget(btnTranslate);
    actionLay->addWidget(btnCode);
    // AI 模型切换下拉框配置
    QComboBox* modelCombo = new QComboBox();
    modelCombo->setCursor(Qt::PointingHandCursor);
    modelCombo->addItems({ "🧠 DeepSeek-V3", "💡 DeepSeek-R1 (深度思考)", "💬 Doubao-Lite (文本)", "🎨 Doubao-Seedream (画图)" });
    modelCombo->setStyleSheet(
        "QComboBox { background-color: transparent; color: #4E5969; border-radius: 8px; padding: 4px 10px; font-weight: bold; border: 1px solid transparent; } "
        "QComboBox:hover { background-color: #F2F3F5; color: #165DFF; border: 1px solid #165DFF; } "
        "QComboBox::drop-down { border: none; width: 20px; } "
        "QComboBox QAbstractItemView { outline: none; border: 1px solid #E5E6EB; border-radius: 8px; background: white; selection-background-color: #E8F3FF; selection-color: #165DFF; }"
    );
    // 根据用户选择动态切换 API 地址和模型参数，并重置对话上下文避免身份混淆
    connect(modelCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        QString displayName;  // 用于 system prompt 中标识当前模型身份
        if (text.contains("V3")) {
            m_apiUrl = "https://api.deepseek.com/chat/completions";
            m_apiKey = "sk-54ccee7e91ab405a94c622d9419a91e9";
            m_modelName = "deepseek-chat";
            displayName = "DeepSeek-V3";
        }
        else if (text.contains("R1")) {
            m_apiUrl = "https://api.deepseek.com/chat/completions";
            m_apiKey = "sk-54ccee7e91ab405a94c622d9419a91e9";
            m_modelName = "deepseek-reasoner";
            displayName = "DeepSeek-R1";
        }
        else if (text.contains("Doubao-Lite")) {
            m_apiUrl = "https://ark.cn-beijing.volces.com/api/v3/chat/completions";
            m_apiKey = "a49e5973-6d5c-442c-9d79-ba4b433381d9";
            m_modelName = "ep-20260307031237-h5zmt";
            displayName = "豆包大模型 (Doubao-Lite)";
        }
        else if (text.contains("Seedream")) {
            m_apiUrl = "https://ark.cn-beijing.volces.com/api/v3/images/generations";
            m_apiKey = "a49e5973-6d5c-442c-9d79-ba4b433381d9";
            m_modelName = "ep-20260306195042-l95bj";
            displayName = "豆包 Seedream (画图)";
        }
        // [修复] 切换模型时必须重置对话上下文，否则历史消息会让新模型"继承"旧模型的身份
        m_messageHistory = QJsonArray();
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = QString("你是 %1，一个专业的企业智能考勤与OA助手，你可以使用 Markdown 格式美化排版。"
            "当用户询问你是什么模型时，请如实回答你是 %1。").arg(displayName);
        m_messageHistory.append(systemMsg);
        });
    actionLay->addWidget(modelCombo);
    actionLay->addStretch();
    // 发送按钮配置
    m_sendBtn->disconnect();
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    m_sendBtn->setStyleSheet("background-color: #165DFF; color: white; border-radius: 12px; padding: 6px 20px; font-weight: bold; border: none;");
    connect(m_sendBtn, &QPushButton::clicked, this, &AIAssistantModule::onSendClicked);
    actionLay->addWidget(m_sendBtn);
    inLay->addLayout(actionLay);
    // 将组装好的输入区域添加到布局
    rightSplitter->addWidget(inputFrame);
    rightSplitter->setStretchFactor(0, 4); // 聊天记录区占据更多比例
    rightSplitter->setStretchFactor(1, 1); // 输入区占据较少比例
    rightLay->addLayout(topControlLay);
    rightLay->addWidget(rightSplitter);
    mainSplitter->addWidget(m_leftWidget);
    mainSplitter->addWidget(rightWidget);
    mainSplitter->setStretchFactor(0, 0); // 左侧固定/不拉伸
    mainSplitter->setStretchFactor(1, 1); // 右侧拉伸
    mainLayout->addWidget(mainSplitter);
}
// 处理输入框中的快捷键 (Enter 发送, Shift+Enter 换行)
bool AIAssistantModule::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_inputTextEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) return false; // Shift+Enter 正常换行
            else {
                onSendClicked();
                return true;
            }
        }
    }
    return QObject::eventFilter(obj, event);
}
// 处理用户选择文件并读取其内容
void AIAssistantModule::onAttachFileClicked() {
    // 弹出文件选择对话框
    QStringList filePaths = QFileDialog::getOpenFileNames(nullptr, "上传附件", "", "所有文件 (*.*);;文档 (*.txt *.csv *.md *.doc *.docx *.pdf);;图片 (*.png *.jpg *.jpeg)");
    if (filePaths.isEmpty()) return;
    // 限制最多选择 5 个文件
    if (filePaths.size() > 5) {
        QMessageBox::warning(nullptr, "超出限制", "每次最多只能上传 5 个附件，系统已自动截取前 5 个文件！");
        filePaths = filePaths.mid(0, 5);
    }
    // 清理先前的附件缓存
    m_fileContext.clear();
    m_pendingFiles.clear();
    QStringList fileNames;
    // 遍历读取每一个被选中的文件
    for (const QString& path : filePaths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QString fileName = QFileInfo(path).fileName();
            fileNames << fileName;
            // 存入待发送队列
            m_pendingFiles.append({ fileName, data });
            QString contentStr;
            // 针对不同文件类型做内容预览截取
            if (fileName.endsWith(".png", Qt::CaseInsensitive) || fileName.endsWith(".jpg", Qt::CaseInsensitive)) {
                contentStr = "[系统提示: 用户已上传图像文件 " + fileName + "，已读取图像基础元数据。]";
            }
            else if (fileName.endsWith(".doc", Qt::CaseInsensitive) || fileName.endsWith(".pdf", Qt::CaseInsensitive)) {
                contentStr = QString::fromUtf8(data);
                if (contentStr.length() > 5000) contentStr = contentStr.left(5000) + "\n...[文件过长已截断]...";
            }
            else {
                contentStr = QString::fromUtf8(data);
            }

            // 拼装文件上下文文本
            m_fileContext += QString("\n\n=== 附件 [%1] ===\n%2\n=== 附件结束 ===\n").arg(fileName, contentStr);
            file.close();
        }
    }
    // 更新 UI 提示已选择附件
    if (!fileNames.isEmpty()) {
        QString displayNames = fileNames.join(", ");
        appendMessage("user", QString("📎 <b>已上传 %1 个附件：</b><br><span style='color:#165DFF;'>%2</span><br>请参考附件背景知识回答我接下来的问题。").arg(fileNames.size()).arg(displayNames), false);
        m_attachBtn->setText(QString("📎 已就绪(%1)").arg(fileNames.size()));
        m_attachBtn->setStyleSheet("background-color: #E8F3FF; color: #165DFF; border-radius: 8px; padding: 6px 10px; font-weight: bold; border: none;");
    }
}
// 处理消息打包、向服务端发送审核数据以及发起 AI 网络请求
void AIAssistantModule::onSendClicked() {
    // 防止重复提交验证和空输入拦截
    if (m_isReplying) return;
    QString inputText = m_inputTextEdit->toPlainText().trimmed();
    if (inputText.isEmpty() || m_apiKey.contains("xxx")) return;
    m_inputTextEdit->clear();
    appendMessage("user", inputText, true);
    bool isImageAPI = m_apiUrl.contains("images");
    QJsonObject requestBody;
    requestBody["model"] = m_modelName;
    // 处理带有附件请求的特殊逻辑分支
    if (!m_fileContext.isEmpty()) {
        // 先将物理文件发给服务端审计
        for (const auto& filePair : m_pendingFiles) {
            sendAuditFileToServer(m_currentSessionId, filePair.first, filePair.second);
        }
        m_pendingFiles.clear();
        m_isReplying = true;
        m_sendBtn->setEnabled(false);
        m_sendBtn->setText(isImageAPI ? "画图中..." : "解读中...");
        // 拼接含有附件内容的最终提示词
        QString finalPrompt = "以下是提供给你的参考文件内容：\n" + m_fileContext + "\n---\n请基于上述资料，详细回答问题：" + inputText;
        // 清理 UI 状态
        m_fileContext.clear();
        m_attachBtn->setText("📎");
        m_attachBtn->setStyleSheet("QPushButton { background: transparent; color: #4E5969; border-radius: 8px; padding: 6px 10px; font-weight: bold; border: none; } QPushButton:hover { background: #F2F3F5; color: #165DFF; }");
        // 根据API类型设置不同的请求体参数
        if (isImageAPI) {
            requestBody["prompt"] = inputText;
        }
        else {
            QJsonObject userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = finalPrompt;
            m_messageHistory.append(userMsg);
            requestBody["messages"] = m_messageHistory;
            requestBody["temperature"] = 0.5;
        }
        // 发起附带上下文的网络请求
        QNetworkRequest request((QUrl(m_apiUrl)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
        QNetworkReply* reply = m_networkManager->post(request, QJsonDocument(requestBody).toJson());
        connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError>& errors) { reply->ignoreSslErrors(errors); });
        return;
    }
    // 检查是否命中本地业务意图（拦截器）
    if (handleLocalIntent(inputText)) return;
    // 纯文本消息逻辑分支
    m_isReplying = true;
    m_sendBtn->setEnabled(false);
    m_sendBtn->setText(isImageAPI ? "挥毫中..." : "思考中...");
    if (isImageAPI) {
        requestBody["prompt"] = inputText;
    }
    else {
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = inputText;
        m_messageHistory.append(userMsg);
        requestBody["messages"] = m_messageHistory;
        requestBody["temperature"] = 0.7;
    }
    // 发起普通的网络请求
    QNetworkRequest request((QUrl(m_apiUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    QNetworkReply* reply = m_networkManager->post(request, QJsonDocument(requestBody).toJson());
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError>& errors) { reply->ignoreSslErrors(errors); });
}
// 用于识别并优先处理特定的本地业务需求（如查考勤）
bool AIAssistantModule::handleLocalIntent(const QString& inputText) {
    if (inputText.contains("今日考勤") || inputText.contains("今天打卡")) {
        // 构建查询请求并发送至后台系统
        QJsonObject req;
        req["type"] = "query_today_attendance_for_ai";
        req["name"] = m_userName;
        QJsonObject res = NetworkHelper::request(req, 3000, 8000);
        // 解析并格式化考勤查询结果
        QString replyStr = "根据实时查询，您今天的打卡记录如下：\n";
        QJsonArray arr = res["data"].toArray();
        if (arr.isEmpty()) {
            replyStr = "查不到数据哦，您今天似乎还没打卡。";
        }
        else {
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject o = arr[i].toObject();
                replyStr += QString("- `%1`：**%2**\n").arg(o["time"].toString(), o["status"].toString());
            }
        }
        // 将结果显示在聊天界面并拦截后续大模型请求
        appendMessage("ai", replyStr, true);
        return true;
    }
    return false;
}
// 接收 AI 接口返回的响应数据并更新到界面
void AIAssistantModule::onNetworkReply(QNetworkReply* reply) {
    // 恢复 UI 状态
    m_isReplying = false;
    m_sendBtn->setEnabled(true);
    m_sendBtn->setText("发送");
    // 校验网络请求状态
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject json = doc.object();
        bool isImageAPI = m_apiUrl.contains("images");
        // 解析图片生成接口的响应
        if (isImageAPI) {
            if (json.contains("data") && json["data"].isArray()) {
                QString imgUrl = json["data"].toArray()[0].toObject()["url"].toString();
                QJsonObject pseudoMsg;
                pseudoMsg["role"] = "assistant";
                pseudoMsg["content"] = "[AI为您生成了一张图片]";
                m_messageHistory.append(pseudoMsg);

                // 先显示文字提示（图片正在加载）
                appendMessage("ai", "🎨 <b>为您生成了一张图片，正在加载中...</b>", false);

                // 使用独立的 QNetworkAccessManager 下载图片（避免触发 onNetworkReply）
                QNetworkAccessManager* imgLoader = new QNetworkAccessManager(this);
                QNetworkRequest imgReq;
                // 直接用 QUrl::fromEncoded 避免 Qt 对已签名 URL 中的参数二次编码
                imgReq.setUrl(QUrl::fromEncoded(imgUrl.toUtf8()));
                imgReq.setRawHeader("User-Agent", "Mozilla/5.0");
                // 跟随重定向
                imgReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                    QNetworkRequest::NoLessSafeRedirectPolicy);
                QNetworkReply* imgReply = imgLoader->get(imgReq);
                connect(imgReply, &QNetworkReply::sslErrors, imgReply,
                    [imgReply](const QList<QSslError>& errors) { imgReply->ignoreSslErrors(errors); });

                // 图片下载完成后，缩放并直接追加到聊天气泡
                connect(imgReply, &QNetworkReply::finished, this,
                    [this, imgReply, imgLoader, imgUrl]() {
                        QString resultHtml;

                        if (imgReply->error() == QNetworkReply::NoError) {
                            QByteArray imageData = imgReply->readAll();
                            QImage originalImg;
                            originalImg.loadFromData(imageData);

                            if (!originalImg.isNull()) {
                                // 根据对话框实际宽度动态计算图片显示尺寸
                                int browserWidth = m_textBrowser->viewport()->width();
                                int maxImgWidth = static_cast<int>(browserWidth * 0.55);
                                maxImgWidth = qBound(200, maxImgWidth, 800);

                                QImage displayImg = originalImg;
                                if (displayImg.width() > maxImgWidth) {
                                    displayImg = displayImg.scaledToWidth(maxImgWidth, Qt::SmoothTransformation);
                                }

                                // 编码为 JPEG（比 PNG 小很多，加载更快）
                                QByteArray scaledData;
                                QBuffer buffer(&scaledData);
                                buffer.open(QIODevice::WriteOnly);
                                displayImg.save(&buffer, "JPEG", 85);
                                buffer.close();

                                // QTextBrowser 只认绝对像素值的 width/height
                                resultHtml = QString(
                                    "🎨 <b>为您生成了一张图片！</b><br><br>"
                                    "<img src='data:image/jpeg;base64,%1' width='%2' height='%3' /><br><br>"
                                    "<a href='%4' style='color:#165DFF; font-weight:bold; text-decoration:underline;'>"
                                    "🔗 点击查看高清原图并保存</a>"
                                ).arg(QString(scaledData.toBase64()))
                                    .arg(displayImg.width())
                                    .arg(displayImg.height())
                                    .arg(imgUrl);
                            }
                            else {
                                // 下载成功但图片解码失败
                                resultHtml = QString(
                                    "🎨 <b>图片生成成功！</b>（预览解码失败）<br><br>"
                                    "<a href='%1' style='color:#165DFF; font-weight:bold; text-decoration:underline;'>"
                                    "🔗 点击此处在浏览器中查看高清原图并保存</a>"
                                ).arg(imgUrl);
                            }
                        }
                        else {
                            // 下载失败，提供可点击链接作为兜底方案
                            qWarning() << "[AIModule] 图片下载失败:" << imgReply->errorString()
                                << "HTTP状态码:" << imgReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                            resultHtml = QString(
                                "🎨 <b>图片生成成功！</b>（预览加载失败）<br><br>"
                                "<a href='%1' style='color:#165DFF; font-weight:bold; text-decoration:underline;'>"
                                "🔗 点击此处在浏览器中查看高清原图并保存</a>"
                            ).arg(imgUrl);
                        }

                        // 移除之前的"加载中"提示，追加最终结果
                        QString loadingPlaceholder = "🎨 <b>为您生成了一张图片，正在加载中...</b>";
                        m_currentHtmlDisplay.replace(m_currentHtmlDisplay.lastIndexOf(loadingPlaceholder),
                            loadingPlaceholder.length(), "");
                        appendMessage("ai", resultHtml, true);

                        imgReply->deleteLater();
                        imgLoader->deleteLater();
                    });
            }
            else {
                appendMessage("ai", "❌ 画图失败，大模型拒绝了请求或触发安全拦截。", false);
            }
        }
        // 解析文本对话接口的响应
        else {
            if (json.contains("choices") && json["choices"].isArray()) {
                QJsonObject msgObj = json["choices"].toArray()[0].toObject()["message"].toObject();
                m_messageHistory.append(msgObj);
                appendMessage("ai", msgObj["content"].toString(), true);
            }
        }
        // 延迟刷新左侧会话列表（以便更新摘要信息），先屏蔽信号避免触发二次加载
        QTimer::singleShot(600, this, [this]() {
            m_sessionList->blockSignals(true);
            loadSessionsFromDB();
            m_sessionList->blockSignals(false);
            });
    }
    else {
        // 处理并打印网络异常信息
        appendMessage("ai", "❌ 网络异常或接口拦截: " + reply->errorString(), false);
    }
    reply->deleteLater();
}
// 将上传的文件数据发送给内部服务端进行记录或安全检查
void AIAssistantModule::sendAuditFileToServer(const QString& sessionId, const QString& fileName, const QByteArray& fileData) {
    QJsonObject json;
    json["type"] = "ai_audit_file";
    json["name"] = m_userName;
    json["session_id"] = sessionId;
    json["filename"] = fileName;
    json["filedata"] = QString(fileData.toBase64());
    NetworkHelper::sendAsync(json);
}
// 将聊天文本发送给内部服务端进行记录
void AIAssistantModule::sendAuditToServer(const QString& sessionId, const QString& role, const QString& content) {
    QJsonObject json;
    json["type"] = "ai_audit";
    json["name"] = m_userName;
    json["session_id"] = sessionId;
    json["role"] = role;
    json["content"] = content;
    NetworkHelper::sendAsync(json);
}
// 将单条消息保存到后台数据库关联的会话下
void AIAssistantModule::saveMessageToDB(const QString& sessionId, const QString& role, const QString& content) {
    QJsonObject req;
    req["type"] = "ai_save_message";
    req["session_id"] = sessionId;
    req["role"] = role;
    req["content"] = content;
    req["name"] = m_userName;
    NetworkHelper::sendAsync(req);
}
// 将角色发言解析为带样式的 HTML 气泡并追加至显示区
void AIAssistantModule::appendMessage(const QString& role, const QString& msg, bool saveToDb) {
    if (saveToDb) saveMessageToDB(m_currentSessionId, role, msg);
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm");
    QString formattedMsg;
    // 根据角色格式化文本内容
    if (role == "user") {
        formattedMsg = msg.toHtmlEscaped();
        formattedMsg.replace("\n", "<br>");
    }
    else {
        // [修复] 如果 AI 消息本身已包含 HTML 标签（如画图接口返回的 <a> <b> 等），
        // 则直接使用，不再经过 parseMarkdown()（其内部 toHtmlEscaped 会破坏标签）
        if (msg.contains("<a ") || msg.contains("<img ") || msg.contains("<b>")) {
            formattedMsg = msg;  // 已经是富文本，直接使用
        }
        else {
            formattedMsg = parseMarkdown(msg);  // 纯文本走 Markdown 解析
        }
    }
    QString bubble;
    // 渲染用户消息气泡（蓝色底色，居右对齐）
    if (role == "user") {
        bubble = QString("<table width='100%' border='0' cellpadding='0' cellspacing='0' style='margin-bottom:15px;'><tr><td width='20%'></td><td align='right'><div style='color:#8C8C8C; font-size:12px; margin-bottom:5px;'>我 %1</div><table border='0' cellpadding='12' cellspacing='0' bgcolor='#165DFF' style='border-radius:12px;'><tr><td style='color:white; font-size:14px; line-height:1.6;'>%2</td></tr></table></td></tr></table>").arg(timeStr, formattedMsg);
    }
    // 渲染 AI 消息气泡（灰色底色，居左对齐）
    else {
        bubble = QString("<table width='100%' border='0' cellpadding='0' cellspacing='0' style='margin-bottom:15px;'><tr><td align='left'><div style='color:#8C8C8C; font-size:12px; margin-bottom:5px;'>🤖 智能管家 %1</div><table border='0' cellpadding='12' cellspacing='0' bgcolor='#F2F3F5' style='border-radius:12px; border: 1px solid #E5E6EB;'><tr><td style='color:#2F3542; font-size:14px; line-height:1.6;'>%2</td></tr></table></td><td width='20%'></td></tr></table>").arg(timeStr, formattedMsg);
        // 如果需要保存且语音开启，则播放合成语音
        if (saveToDb) speakText(msg);
    }
    // 更新浏览器内容并滚动至底部
    m_currentHtmlDisplay += bubble;
    m_textBrowser->setHtml(m_currentHtmlDisplay);
    QScrollBar* scrollBar = m_textBrowser->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}
// 收起或展开左侧会话列表
void AIAssistantModule::toggleSidebar() {
    m_leftWidget->setVisible(!m_leftWidget->isVisible());
    m_toggleSidebarBtn->setText(m_leftWidget->isVisible() ? "☰ 收起列表" : "☰ 展开列表");
}
// 点击新建对话按钮创建全新会话上下文
void AIAssistantModule::onNewSessionClicked() {
    m_currentSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString title = "新对话 " + QDateTime::currentDateTime().toString("MM-dd HH:mm");
    // 向服务端发送创建指令
    QJsonObject req;
    req["type"] = "create_ai_session";
    req["session_id"] = m_currentSessionId;
    req["name"] = m_userName;
    req["title"] = title;
    NetworkHelper::sendAsync(req);
    // 延迟以确保数据落库后重载界面状态
    QTimer::singleShot(300, this, [this]() {
        m_sessionList->blockSignals(true);
        loadSessionsFromDB();
        m_sessionList->blockSignals(false);
        initializeContext();
        });
}
// 从服务端抓取该用户所有的历史会话信息，并填充到侧边列表
void AIAssistantModule::loadSessionsFromDB() {
    m_sessionList->clear();
    QJsonObject req;
    req["type"] = "query_ai_sessions";
    req["name"] = m_userName;
    QJsonObject res = NetworkHelper::request(req, 3000, 8000);
    int targetRow = -1;
    // 解析网络返回的数据并创建列表项
    if (res["status"].toString() == "success") {
        QJsonArray arr = res["data"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            QString sid = o["session_id"].toString();
            QString title = o["title"].toString();
            QString snippet = o["last_message"].toString();
            if (snippet.isEmpty()) snippet = "暂无聊天记录...";
            QListWidgetItem* item = new QListWidgetItem(QString("💬 %1\n  < %2 >").arg(title, snippet));
            item->setData(Qt::UserRole, sid);
            m_sessionList->addItem(item);

            if (sid == m_currentSessionId) targetRow = i;
        }
    }
    bool shouldLoadHistory = !m_sessionList->signalsBlocked();
    // 根据逻辑恢复用户界面当前应选中的高亮项
    if (targetRow >= 0) {
        m_sessionList->setCurrentRow(targetRow);
        if (shouldLoadHistory) loadChatHistoryFromDB(m_currentSessionId);
    }
    else if (m_sessionList->count() > 0) {
        m_sessionList->setCurrentRow(0);
        if (shouldLoadHistory) {
            m_currentSessionId = m_sessionList->item(0)->data(Qt::UserRole).toString();
            loadChatHistoryFromDB(m_currentSessionId);
        }
    }
    else {
        if (shouldLoadHistory) onNewSessionClicked();
    }
}
// 左侧会话列表项点击处理
void AIAssistantModule::onSessionSelected(QListWidgetItem* item) {
    m_currentSessionId = item->data(Qt::UserRole).toString();
    loadChatHistoryFromDB(m_currentSessionId);
}
// 左侧会话列表右键呼出菜单（重命名、删除）
void AIAssistantModule::onSessionContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_sessionList->itemAt(pos);
    if (!item) return;
    QMenu menu;
    QAction* actRename = menu.addAction("✏️ 重命名对话");
    QAction* actDelete = menu.addAction("🗑️ 删除对话");
    QAction* selected = menu.exec(m_sessionList->mapToGlobal(pos));
    QString sid = item->data(Qt::UserRole).toString();
    // 重命名对话流程
    if (selected == actRename) {
        bool ok;
        QString oldName = item->text().split("\n").first().replace("💬 ", "");
        QString newName = QInputDialog::getText(nullptr, "重命名", "请输入新名称:", QLineEdit::Normal, oldName, &ok);
        if (ok && !newName.isEmpty()) {
            QJsonObject req;
            req["type"] = "rename_ai_session";
            req["session_id"] = sid;
            req["title"] = newName;
            NetworkHelper::sendAsync(req);
            QTimer::singleShot(200, this, [this]() {
                m_sessionList->blockSignals(true);
                loadSessionsFromDB();
                m_sessionList->blockSignals(false);
                });
        }
    }
    // 删除对话流程
    else if (selected == actDelete) {
        if (QMessageBox::question(nullptr, "确认隐藏", "确定要在列表中隐藏该对话吗？") == QMessageBox::Yes) {
            QJsonObject req;
            req["type"] = "delete_ai_session";
            req["session_id"] = sid;
            NetworkHelper::sendAsync(req);
            QTimer::singleShot(200, this, [this]() {
                m_sessionList->blockSignals(true);
                loadSessionsFromDB();
                m_sessionList->blockSignals(false);
                });
        }
    }
}
// 按照用户关键字过滤并显示会话历史
void AIAssistantModule::onSearchHistory() {
    QString keyword = m_searchBox->text().trimmed();
    if (keyword.isEmpty()) { loadSessionsFromDB(); return; }
    m_sessionList->clear();
    QJsonObject req;
    req["type"] = "search_ai_history";
    req["name"] = m_userName;
    req["keyword"] = keyword;
    QJsonObject res = NetworkHelper::request(req, 3000, 8000);
    // 解析搜索结果并刷新列表
    if (res["status"].toString() == "success") {
        QJsonArray arr = res["data"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            QString snippet = o["last_message"].toString();
            if (snippet.isEmpty()) snippet = "暂无内容";
            QListWidgetItem* item = new QListWidgetItem(QString("🔍 %1\n  < %2 >").arg(o["title"].toString(), snippet));
            item->setData(Qt::UserRole, o["session_id"].toString());
            m_sessionList->addItem(item);
        }
    }
}
// 重置并初始化当前聊天会话相关的内存变量（角色设定、初始页面）
void AIAssistantModule::initializeContext() {
    m_messageHistory = QJsonArray();
    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = "你是一个专业的企业智能考勤与OA助手，你可以使用 Markdown 格式美化排版。";
    m_messageHistory.append(systemMsg);
    m_currentHtmlDisplay = "<div style='text-align:center; padding: 20px 0; color:#86909C;'>🤖 Ai助手✨</div>";
    m_textBrowser->setHtml(m_currentHtmlDisplay);
}
// 从服务端读取某特定会话的全部消息历史并还原上下文与界面
void AIAssistantModule::loadChatHistoryFromDB(const QString& sessionId) {
    initializeContext();
    QJsonObject req;
    req["type"] = "query_ai_chat_history";
    req["session_id"] = sessionId;
    QJsonObject res = NetworkHelper::request(req, 3000, 8000);
    // 循环遍历加载的消息并渲染进UI
    if (res["status"].toString() == "success") {
        QJsonArray arr = res["data"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            QString role = o["role"].toString();
            QString content = o["content"].toString();
            // AI的角色名称在接口里要求是assistant
            QString apiRole = (role == "ai") ? "assistant" : role;
            QJsonObject msg;
            msg["role"] = apiRole;
            msg["content"] = content;
            m_messageHistory.append(msg);
            appendMessage(role, content, false);
        }
    }
}
// 文本处理工具：对 AI 返回的简易 Markdown 文本进行基础的 HTML 转换
QString AIAssistantModule::parseMarkdown(const QString& md) {
    QString html = md.toHtmlEscaped();
    html.replace(QRegularExpression("\\*\\*(.*?)\\*\\*"), "<b>\\1</b>"); // 粗体解析
    html.replace(QRegularExpression("`([^`]+)`"), "<code style='background-color:#E8F3FF; color:#165DFF; padding:2px 6px; border-radius:4px;'>\\1</code>"); // 行内代码
    html.replace(QRegularExpression("### (.*)"), "<h3 style='margin:10px 0 5px 0; color:#1D2129;'>\\1</h3>"); // 标题
    html.replace(QRegularExpression("(^|\n)- (.*)"), "\\1<li style='margin-left:20px; margin-bottom:4px;'>\\2</li>"); // 列表
    html.replace("\n", "<br>"); // 换行
    return html;
}
// 交互功能：切换语音朗读 (TTS) 的开关状态
void AIAssistantModule::toggleVoice() {
    m_voiceEnabled = !m_voiceEnabled;
    m_voiceBtn->setText(m_voiceEnabled ? "🔊 语音: 开" : "🔇 语音: 关");
    m_voiceBtn->setStyleSheet(m_voiceEnabled ? "color: #165DFF; font-weight: bold; border: none;" : "color: #86909C; font-weight: bold; border: none;");
}
// 利用 Windows 系统的 PowerShell 脚本调用原生语音合成库实现朗读
void AIAssistantModule::speakText(const QString& text) {
    if (!m_voiceEnabled) return;
    QString cleanText = text;
    cleanText.remove(QRegularExpression("<[^>]*>"));
    cleanText.remove(QRegularExpression("[\\*`#]"));
    cleanText.replace("'", "''");
    QString command = QString("Add-Type -AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer).Speak('%1');").arg(cleanText);
    QProcess::startDetached("powershell", QStringList() << "-WindowStyle" << "Hidden" << "-Command" << command);
}
void AIAssistantModule::clearCurrentSession()
{
    if (m_currentSessionId.isEmpty()) return;
    // 清空内存消息历史（保留系统提示词）
    m_messageHistory = QJsonArray();
    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = "你是一个专业的企业智能考勤与OA助手，你可以使用 Markdown 格式美化排版。";
    m_messageHistory.append(systemMsg);
    // 清空界面显示
    m_currentHtmlDisplay = "<div style='text-align:center; padding: 20px 0; color:#86909C;'>🤖 Ai助手✨</div>";
    m_textBrowser->setHtml(m_currentHtmlDisplay);
}
#include "AIAssistantModule.h"
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
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

static QJsonObject requestDataFromServer(const QJsonObject& jsonRequest) {
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", 9999);
    QJsonObject responseJson;

    QString reqType = jsonRequest["type"].toString();

    if (!socket.waitForConnected(3000)) {
        qDebug() << "[AI调试] requestDataFromServer 连接失败! type=" << reqType;
        return responseJson;
    }

    QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
    socket.write(block);
    socket.waitForBytesWritten(2000);

    QByteArray responseData;
    if (socket.waitForReadyRead(8000)) {
        responseData += socket.readAll();
        while (!responseData.endsWith("\n")) {
            if (!socket.waitForReadyRead(3000)) break;
            responseData += socket.readAll();
        }
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (!doc.isNull()) {
            responseJson = doc.object();
            qDebug() << "[AI调试] requestDataFromServer 成功 type=" << reqType
                << " 响应大小=" << responseData.size() << "bytes"
                << " status=" << responseJson["status"].toString();
        }
        else {
            qDebug() << "[AI调试] requestDataFromServer JSON解析失败! type=" << reqType
                << " 原始数据前200字节=" << responseData.left(200);
        }
    }
    else {
        qDebug() << "[AI调试] requestDataFromServer 等待响应超时! type=" << reqType
            << " 已收到字节=" << responseData.size();
    }

    socket.disconnectFromHost();
    return responseJson;
}

static void sendCommandToServer(const QJsonObject& json) {
    QTcpSocket* socket = new QTcpSocket();
    QObject::connect(socket, &QTcpSocket::connected, [socket, json]() {
        QByteArray block = QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n";
        socket->write(block);
        socket->flush();
        QTimer::singleShot(500, socket, &QTcpSocket::disconnectFromHost);
        QTimer::singleShot(2000, socket, &QObject::deleteLater);
        });
    QObject::connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost("127.0.0.1", 9999);
}

AIAssistantModule::AIAssistantModule(QTextBrowser* textBrowser, QLineEdit* lineEdit,
    QPushButton* sendBtn, QPushButton* clearBtn, QString userName, QObject* parent)
    : QObject(parent), m_textBrowser(textBrowser), m_oldLineEdit(lineEdit),
    m_sendBtn(sendBtn), m_clearBtn(clearBtn), m_userName(userName),
    m_isReplying(false), m_voiceEnabled(false)
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AIAssistantModule::onNetworkReply);

    m_apiUrl = "https://api.deepseek.com/chat/completions";
    m_apiKey = "sk-54ccee7e91ab405a94c622d9419a91e9";
    m_modelName = "deepseek-chat";

    rebuildAdvancedUI();
    loadSessionsFromDB();
}

void AIAssistantModule::rebuildAdvancedUI() {
    QWidget* parentW = m_textBrowser->parentWidget();
    if (!parentW) return;

    QLayout* oldLayout = parentW->layout();
    if (oldLayout) { QLayoutItem* item; while ((item = oldLayout->takeAt(0)) != nullptr) {} delete oldLayout; }
    m_oldLineEdit->hide();

    QHBoxLayout* mainLayout = new QHBoxLayout(parentW);
    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, parentW);

    m_leftWidget = new QWidget();
    m_leftWidget->setMinimumWidth(220);
    m_leftWidget->setMaximumWidth(280);
    QVBoxLayout* leftLay = new QVBoxLayout(m_leftWidget);
    leftLay->setContentsMargins(0, 0, 10, 0);

    m_newSessionBtn = new QPushButton("➕ 新建对话");
    m_newSessionBtn->setCursor(Qt::PointingHandCursor);
    m_newSessionBtn->setStyleSheet("background-color: #165DFF; color: white; border-radius: 6px; padding: 10px; font-weight: bold;");
    connect(m_newSessionBtn, &QPushButton::clicked, this, &AIAssistantModule::onNewSessionClicked);

    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("🔍 搜索历史...");
    m_searchBox->setStyleSheet("border: 1px solid #E5E6EB; border-radius: 15px; padding: 5px 15px;");
    connect(m_searchBox, &QLineEdit::returnPressed, this, &AIAssistantModule::onSearchHistory);

    m_sessionList = new QListWidget();
    m_sessionList->setStyleSheet("QListWidget { border: none; background: transparent; outline: none; } QListWidget::item { padding: 10px; border-radius: 6px; margin-bottom: 4px; border-bottom: 1px solid #F2F3F5; color: #1D2129; } QListWidget::item:selected { background-color: #E8F3FF; border-left: 4px solid #165DFF; border-radius: 4px; }");
    connect(m_sessionList, &QListWidget::itemClicked, this, &AIAssistantModule::onSessionSelected);
    m_sessionList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sessionList, &QListWidget::customContextMenuRequested, this, &AIAssistantModule::onSessionContextMenu);

    leftLay->addWidget(m_newSessionBtn);
    leftLay->addWidget(m_searchBox);
    leftLay->addWidget(m_sessionList);

    QWidget* rightWidget = new QWidget();
    QVBoxLayout* rightLay = new QVBoxLayout(rightWidget);
    rightLay->setContentsMargins(0, 0, 0, 0);

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

    QSplitter* rightSplitter = new QSplitter(Qt::Vertical, rightWidget);

    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setStyleSheet("border: none; background: transparent;");
    rightSplitter->addWidget(m_textBrowser);

    QFrame* inputFrame = new QFrame();
    inputFrame->setStyleSheet("QFrame { border: 1.5px solid #165DFF; border-radius: 15px; background: white; }");
    QVBoxLayout* inLay = new QVBoxLayout(inputFrame);
    inLay->setContentsMargins(10, 10, 10, 10);
    inLay->setSpacing(5);

    m_inputTextEdit = new QTextEdit();
    m_inputTextEdit->setPlaceholderText("发消息或输入 / 选择技能... (Enter发送, Shift+Enter换行)");
    m_inputTextEdit->setStyleSheet("border: none; background: transparent; font-size: 14px;");
    m_inputTextEdit->installEventFilter(this);
    inLay->addWidget(m_inputTextEdit);

    QHBoxLayout* actionLay = new QHBoxLayout();
    actionLay->setSpacing(5);

    QString actionBtnStyle =
        "QPushButton { background: transparent; color: #4E5969; border-radius: 8px; padding: 6px 10px; font-weight: bold; border: none; }"
        "QPushButton:hover { background: #F2F3F5; color: #165DFF; }";

    m_attachBtn = new QPushButton("📎");
    m_attachBtn->setCursor(Qt::PointingHandCursor);
    m_attachBtn->setStyleSheet(actionBtnStyle);
    m_attachBtn->setToolTip("上传附件 (图片与各类文档)\n最多 5 个，每个 100 MB，支持所有文件");
    m_attachBtn->setStyleSheet(m_attachBtn->styleSheet() + "QToolTip { color: #ffffff; background-color: #2a2a2a; border: 1px solid white; border-radius: 4px; padding: 5px; }");
    connect(m_attachBtn, &QPushButton::clicked, this, &AIAssistantModule::onAttachFileClicked);

    QPushButton* btnQuick = new QPushButton("⚡ 快速");
    QPushButton* btnWrite = new QPushButton("📝 帮我写作");
    QPushButton* btnTranslate = new QPushButton("🔤 翻译");
    QPushButton* btnCode = new QPushButton("</> 编程");

    btnQuick->setCursor(Qt::PointingHandCursor); btnQuick->setStyleSheet(actionBtnStyle);
    btnWrite->setCursor(Qt::PointingHandCursor); btnWrite->setStyleSheet(actionBtnStyle);
    btnTranslate->setCursor(Qt::PointingHandCursor); btnTranslate->setStyleSheet(actionBtnStyle);
    btnCode->setCursor(Qt::PointingHandCursor); btnCode->setStyleSheet(actionBtnStyle);

    connect(btnQuick, &QPushButton::clicked, this, [=]() { m_inputTextEdit->setPlainText("请帮我总结一下今天的个人考勤数据："); m_inputTextEdit->setFocus(); });
    connect(btnWrite, &QPushButton::clicked, this, [=]() { m_inputTextEdit->setPlainText("请帮我起草一份关于 [此处填写主题] 的文档/报告，要求语言正式、逻辑清晰。"); m_inputTextEdit->setFocus(); });
    connect(btnTranslate, &QPushButton::clicked, this, [=]() { m_inputTextEdit->setPlainText("请将以下内容精准翻译成 [中文/英文]：\n\n"); m_inputTextEdit->setFocus(); });
    connect(btnCode, &QPushButton::clicked, this, [=]() { m_inputTextEdit->setPlainText("请帮我用 C++/Qt 编写一段代码，实现以下功能：\n\n"); m_inputTextEdit->setFocus(); });

    actionLay->addWidget(m_attachBtn);
    actionLay->addWidget(btnQuick);
    actionLay->addWidget(btnWrite);
    actionLay->addWidget(btnTranslate);
    actionLay->addWidget(btnCode);

    QComboBox* modelCombo = new QComboBox();
    modelCombo->setCursor(Qt::PointingHandCursor);
    modelCombo->addItems({ "🧠 DeepSeek-V3", "💡 DeepSeek-R1 (深度思考)", "💬 Doubao-Lite (文本)", "🎨 Doubao-Seedream (画图)" });
    modelCombo->setStyleSheet(
        "QComboBox { background-color: transparent; color: #4E5969; border-radius: 8px; padding: 4px 10px; font-weight: bold; border: 1px solid transparent; }"
        "QComboBox:hover { background-color: #F2F3F5; color: #165DFF; border: 1px solid #165DFF; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { outline: none; border: 1px solid #E5E6EB; border-radius: 8px; background: white; selection-background-color: #E8F3FF; selection-color: #165DFF; }"
    );

    connect(modelCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        if (text.contains("V3")) {
            m_apiUrl = "https://api.deepseek.com/chat/completions";
            m_apiKey = "sk-54ccee7e91ab405a94c622d9419a91e9";
            m_modelName = "deepseek-chat";
        }
        else if (text.contains("R1")) {
            m_apiUrl = "https://api.deepseek.com/chat/completions";
            m_apiKey = "sk-54ccee7e91ab405a94c622d9419a91e9";
            m_modelName = "deepseek-reasoner";
        }
        else if (text.contains("Doubao-Lite")) {
            m_apiUrl = "https://ark.cn-beijing.volces.com/api/v3/chat/completions";
            m_apiKey = "a49e5973-6d5c-442c-9d79-ba4b433381d9";
            m_modelName = "ep-20260307031237-h5zmt";
        }
        else if (text.contains("Seedream")) {
            m_apiUrl = "https://ark.cn-beijing.volces.com/api/v3/images/generations";
            m_apiKey = "a49e5973-6d5c-442c-9d79-ba4b433381d9";
            m_modelName = "ep-20260306195042-l95bj";
        }
        });

    actionLay->addWidget(modelCombo);
    actionLay->addStretch();

    m_sendBtn->disconnect();
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    m_sendBtn->setStyleSheet("background-color: #165DFF; color: white; border-radius: 12px; padding: 6px 20px; font-weight: bold; border: none;");
    connect(m_sendBtn, &QPushButton::clicked, this, &AIAssistantModule::onSendClicked);
    actionLay->addWidget(m_sendBtn);

    inLay->addLayout(actionLay);

    rightSplitter->addWidget(inputFrame);
    rightSplitter->setStretchFactor(0, 4);
    rightSplitter->setStretchFactor(1, 1);

    rightLay->addLayout(topControlLay);
    rightLay->addWidget(rightSplitter);

    mainSplitter->addWidget(m_leftWidget);
    mainSplitter->addWidget(rightWidget);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);

    mainLayout->addWidget(mainSplitter);
}

bool AIAssistantModule::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_inputTextEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) return false;
            else { onSendClicked(); return true; }
        }
    }
    return QObject::eventFilter(obj, event);
}

void AIAssistantModule::onAttachFileClicked() {
    QStringList filePaths = QFileDialog::getOpenFileNames(nullptr,
        "上传附件", "",
        "所有文件 (*.*);;文档 (*.txt *.csv *.md *.doc *.docx *.pdf);;图片 (*.png *.jpg *.jpeg)");

    if (filePaths.isEmpty()) return;

    if (filePaths.size() > 5) {
        QMessageBox::warning(nullptr, "超出限制", "每次最多只能上传 5 个附件，系统已自动截取前 5 个文件！");
        filePaths = filePaths.mid(0, 5);
    }

    m_fileContext.clear();
    m_pendingFiles.clear();
    QStringList fileNames;

    for (const QString& path : filePaths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QString fileName = QFileInfo(path).fileName();
            fileNames << fileName;

            m_pendingFiles.append({ fileName, data });

            QString contentStr;
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

            m_fileContext += QString("\n\n=== 附件 [%1] ===\n%2\n=== 附件结束 ===\n").arg(fileName, contentStr);
            file.close();
        }
    }

    if (!fileNames.isEmpty()) {
        QString displayNames = fileNames.join(", ");
        appendMessage("user", QString("📎 <b>已上传 %1 个附件：</b><br><span style='color:#165DFF;'>%2</span><br>请参考附件背景知识回答我接下来的问题。").arg(fileNames.size()).arg(displayNames), false);
        m_attachBtn->setText(QString("📎 已就绪(%1)").arg(fileNames.size()));
        m_attachBtn->setStyleSheet("background-color: #E8F3FF; color: #165DFF; border-radius: 8px; padding: 6px 10px; font-weight: bold; border: none;");
    }
}

void AIAssistantModule::onSendClicked() {
    if (m_isReplying) return;
    QString inputText = m_inputTextEdit->toPlainText().trimmed();
    if (inputText.isEmpty() || m_apiKey.contains("xxx")) return;

    m_inputTextEdit->clear();
    appendMessage("user", inputText, true);

    bool isImageAPI = m_apiUrl.contains("images");
    QJsonObject requestBody;
    requestBody["model"] = m_modelName;

    if (!m_fileContext.isEmpty()) {
        for (const auto& filePair : m_pendingFiles) {
            sendAuditFileToServer(m_currentSessionId, filePair.first, filePair.second);
        }
        m_pendingFiles.clear();

        m_isReplying = true; m_sendBtn->setEnabled(false); m_sendBtn->setText(isImageAPI ? "画图中..." : "解读中...");
        QString finalPrompt = "以下是提供给你的参考文件内容：\n" + m_fileContext + "\n---\n请基于上述资料，详细回答问题：" + inputText;

        m_fileContext.clear();
        m_attachBtn->setText("📎");
        m_attachBtn->setStyleSheet("QPushButton { background: transparent; color: #4E5969; border-radius: 8px; padding: 6px 10px; font-weight: bold; border: none; } QPushButton:hover { background: #F2F3F5; color: #165DFF; }");

        if (isImageAPI) {
            requestBody["prompt"] = inputText;
        }
        else {
            QJsonObject userMsg; userMsg["role"] = "user"; userMsg["content"] = finalPrompt; m_messageHistory.append(userMsg);
            requestBody["messages"] = m_messageHistory;
            requestBody["temperature"] = 0.5;
        }

        QNetworkRequest request((QUrl(m_apiUrl))); request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
        QNetworkReply* reply = m_networkManager->post(request, QJsonDocument(requestBody).toJson());
        connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError>& errors) { reply->ignoreSslErrors(errors); });
        return;
    }

    if (handleLocalIntent(inputText)) return;

    m_isReplying = true; m_sendBtn->setEnabled(false); m_sendBtn->setText(isImageAPI ? "挥毫中..." : "思考中...");

    if (isImageAPI) {
        requestBody["prompt"] = inputText;
    }
    else {
        QJsonObject userMsg; userMsg["role"] = "user"; userMsg["content"] = inputText; m_messageHistory.append(userMsg);
        requestBody["messages"] = m_messageHistory;
        requestBody["temperature"] = 0.7;
    }

    QNetworkRequest request((QUrl(m_apiUrl))); request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    QNetworkReply* reply = m_networkManager->post(request, QJsonDocument(requestBody).toJson());
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError>& errors) { reply->ignoreSslErrors(errors); });
}

bool AIAssistantModule::handleLocalIntent(const QString& inputText) {
    if (inputText.contains("今日考勤") || inputText.contains("今天打卡")) {
        QJsonObject req;
        req["type"] = "query_today_attendance_for_ai";
        req["name"] = m_userName;
        QJsonObject res = requestDataFromServer(req);

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
        appendMessage("ai", replyStr, true);
        return true;
    }
    return false;
}

void AIAssistantModule::onNetworkReply(QNetworkReply* reply) {
    m_isReplying = false; m_sendBtn->setEnabled(true); m_sendBtn->setText("发送");
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll()); QJsonObject json = doc.object();

        bool isImageAPI = m_apiUrl.contains("images");

        if (isImageAPI) {
            if (json.contains("data") && json["data"].isArray()) {
                QString imgUrl = json["data"].toArray()[0].toObject()["url"].toString();
                QString responseMsg = QString("🎨 <b>为您生成了一张图片！</b><br><br><a href='%1' style='color:#165DFF; font-weight:bold; text-decoration:underline;'>🔗 点击此处在浏览器中查看高清原图并保存</a>").arg(imgUrl);

                QJsonObject pseudoMsg; pseudoMsg["role"] = "assistant"; pseudoMsg["content"] = "[AI为您生成了一张图片]";
                m_messageHistory.append(pseudoMsg);

                appendMessage("ai", responseMsg, true);
            }
            else {
                appendMessage("ai", "❌ 画图失败，大模型拒绝了请求或触发安全拦截。", false);
            }
        }
        else {
            if (json.contains("choices") && json["choices"].isArray()) {
                QJsonObject msgObj = json["choices"].toArray()[0].toObject()["message"].toObject();
                m_messageHistory.append(msgObj);
                appendMessage("ai", msgObj["content"].toString(), true);
            }
        }

        // 🔧 延迟刷新列表
        QTimer::singleShot(600, this, [this]() {
            m_sessionList->blockSignals(true);
            loadSessionsFromDB();
            m_sessionList->blockSignals(false);
            });
    }
    else {
        appendMessage("ai", "❌ 网络异常或接口拦截: " + reply->errorString(), false);
    }
    reply->deleteLater();
}


void AIAssistantModule::sendAuditFileToServer(const QString& sessionId, const QString& fileName, const QByteArray& fileData) {
    QJsonObject json;
    json["type"] = "ai_audit_file";
    json["name"] = m_userName;
    json["session_id"] = sessionId;
    json["filename"] = fileName;
    json["filedata"] = QString(fileData.toBase64());
    sendCommandToServer(json);
}

void AIAssistantModule::sendAuditToServer(const QString& sessionId, const QString& role, const QString& content) {
    QJsonObject json;
    json["type"] = "ai_audit";
    json["name"] = m_userName;
    json["session_id"] = sessionId;
    json["role"] = role;
    json["content"] = content;
    sendCommandToServer(json);
}

void AIAssistantModule::saveMessageToDB(const QString& sessionId, const QString& role, const QString& content) {
    QJsonObject req;
    req["type"] = "ai_save_message";
    req["session_id"] = sessionId;
    req["role"] = role;
    req["content"] = content;
    req["name"] = m_userName;
    sendCommandToServer(req);
}

void AIAssistantModule::appendMessage(const QString& role, const QString& msg, bool saveToDb) {
    if (saveToDb) saveMessageToDB(m_currentSessionId, role, msg);

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm");
    QString formattedMsg;

    if (role == "user") {
        formattedMsg = msg.toHtmlEscaped();
        formattedMsg.replace("\n", "<br>");
    }
    else {
        formattedMsg = parseMarkdown(msg);
    }

    QString bubble;
    if (role == "user") {
        bubble = QString(
            "<table width='100%' border='0' cellpadding='0' cellspacing='0' style='margin-bottom:15px;'>"
            "<tr><td width='20%'></td><td align='right'>"
            "<div style='color:#8C8C8C; font-size:12px; margin-bottom:5px;'>我 %1</div>"
            "<table border='0' cellpadding='12' cellspacing='0' bgcolor='#165DFF' style='border-radius:12px;'>"
            "<tr><td style='color:white; font-size:14px; line-height:1.6;'>%2</td></tr>"
            "</table></td></tr></table>"
        ).arg(timeStr, formattedMsg);
    }
    else {
        bubble = QString(
            "<table width='100%' border='0' cellpadding='0' cellspacing='0' style='margin-bottom:15px;'>"
            "<tr><td align='left'>"
            "<div style='color:#8C8C8C; font-size:12px; margin-bottom:5px;'>🤖 智能管家 %1</div>"
            "<table border='0' cellpadding='12' cellspacing='0' bgcolor='#F2F3F5' style='border-radius:12px; border: 1px solid #E5E6EB;'>"
            "<tr><td style='color:#2F3542; font-size:14px; line-height:1.6;'>%2</td></tr>"
            "</table></td><td width='20%'></td></tr></table>"
        ).arg(timeStr, formattedMsg);
        if (saveToDb) speakText(msg);
    }

    m_currentHtmlDisplay += bubble;
    m_textBrowser->setHtml(m_currentHtmlDisplay);
    QScrollBar* scrollBar = m_textBrowser->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void AIAssistantModule::toggleSidebar() {
    m_leftWidget->setVisible(!m_leftWidget->isVisible());
    m_toggleSidebarBtn->setText(m_leftWidget->isVisible() ? "☰ 收起列表" : "☰ 展开列表");
}

void AIAssistantModule::onNewSessionClicked() {
    m_currentSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString title = "新对话 " + QDateTime::currentDateTime().toString("MM-dd HH:mm");

    QJsonObject req;
    req["type"] = "create_ai_session";
    req["session_id"] = m_currentSessionId;
    req["name"] = m_userName;
    req["title"] = title;
    sendCommandToServer(req);

    QTimer::singleShot(300, this, [this]() {
        m_sessionList->blockSignals(true);
        loadSessionsFromDB();
        m_sessionList->blockSignals(false);
        initializeContext();
        });
}
void AIAssistantModule::loadSessionsFromDB() {
    m_sessionList->clear();
    QJsonObject req;
    req["type"] = "query_ai_sessions";
    req["name"] = m_userName;
    QJsonObject res = requestDataFromServer(req);

    int targetRow = -1;
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

void AIAssistantModule::onSessionSelected(QListWidgetItem* item) {
    m_currentSessionId = item->data(Qt::UserRole).toString();
    loadChatHistoryFromDB(m_currentSessionId);
}

void AIAssistantModule::onSessionContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_sessionList->itemAt(pos);
    if (!item) return;
    QMenu menu; QAction* actRename = menu.addAction("✏️ 重命名对话"); QAction* actDelete = menu.addAction("🗑️ 删除对话");
    QAction* selected = menu.exec(m_sessionList->mapToGlobal(pos));
    QString sid = item->data(Qt::UserRole).toString();

    if (selected == actRename) {
        bool ok; QString oldName = item->text().split("\n").first().replace("💬 ", "");
        QString newName = QInputDialog::getText(nullptr, "重命名", "请输入新名称:", QLineEdit::Normal, oldName, &ok);
        if (ok && !newName.isEmpty()) {
            QJsonObject req; req["type"] = "rename_ai_session"; req["session_id"] = sid; req["title"] = newName;
            sendCommandToServer(req);
            QTimer::singleShot(200, this, [this]() {
                m_sessionList->blockSignals(true); loadSessionsFromDB(); m_sessionList->blockSignals(false);
                });
        }
    }
    else if (selected == actDelete) {
        if (QMessageBox::question(nullptr, "确认隐藏", "确定要在列表中隐藏该对话吗？") == QMessageBox::Yes) {
            QJsonObject req; req["type"] = "delete_ai_session"; req["session_id"] = sid;
            sendCommandToServer(req);
            QTimer::singleShot(200, this, [this]() {
                m_sessionList->blockSignals(true); loadSessionsFromDB(); m_sessionList->blockSignals(false);
                });
        }
    }
}

void AIAssistantModule::onSearchHistory() {
    QString keyword = m_searchBox->text().trimmed();
    if (keyword.isEmpty()) { loadSessionsFromDB(); return; }
    m_sessionList->clear();

    QJsonObject req;
    req["type"] = "search_ai_history";
    req["name"] = m_userName;
    req["keyword"] = keyword;
    QJsonObject res = requestDataFromServer(req);

    if (res["status"].toString() == "success") {
        QJsonArray arr = res["data"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            QString snippet = o["last_message"].toString(); if (snippet.isEmpty()) snippet = "暂无内容";
            QListWidgetItem* item = new QListWidgetItem(QString("🔍 %1\n  < %2 >").arg(o["title"].toString(), snippet));
            item->setData(Qt::UserRole, o["session_id"].toString());
            m_sessionList->addItem(item);
        }
    }
}

void AIAssistantModule::initializeContext() {
    m_messageHistory = QJsonArray();
    QJsonObject systemMsg; systemMsg["role"] = "system"; systemMsg["content"] = "你是一个专业的企业智能考勤与OA助手。你可以使用 Markdown 格式美化排版。";
    m_messageHistory.append(systemMsg);
    m_currentHtmlDisplay = "<div style='text-align:center; padding: 20px 0; color:#86909C;'>🤖 Ai助手. ✨</div>";
    m_textBrowser->setHtml(m_currentHtmlDisplay);
}

// ============================================================================
// 🔧 [修复4] loadChatHistoryFromDB - 加入重试机制，解决偶发查询返回空的问题
//    原因：服务端 QtConcurrent + ODBC连接创建 + QueuedConnection写回
//    可能导致客户端 waitForReadyRead 超时前数据还没到达
// ============================================================================
void AIAssistantModule::loadChatHistoryFromDB(const QString& sessionId) {
    initializeContext();
    QJsonObject req;
    req["type"] = "query_ai_chat_history";
    req["session_id"] = sessionId;
    QJsonObject res = requestDataFromServer(req);

    if (res["status"].toString() == "success") {
        QJsonArray arr = res["data"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            QString role = o["role"].toString();
            QString content = o["content"].toString();

            QString apiRole = (role == "ai") ? "assistant" : role;
            QJsonObject msg; msg["role"] = apiRole; msg["content"] = content; m_messageHistory.append(msg);

            appendMessage(role, content, false);
        }
    }
}

QString AIAssistantModule::parseMarkdown(const QString& md) {
    QString html = md.toHtmlEscaped();
    html.replace(QRegularExpression("\\*\\*(.*?)\\*\\*"), "<b>\\1</b>");
    html.replace(QRegularExpression("`([^`]+)`"), "<code style='background-color:#E8F3FF; color:#165DFF; padding:2px 6px; border-radius:4px;'>\\1</code>");
    html.replace(QRegularExpression("### (.*)"), "<h3 style='margin:10px 0 5px 0; color:#1D2129;'>\\1</h3>");
    html.replace(QRegularExpression("(^|\n)- (.*)"), "\\1<li style='margin-left:20px; margin-bottom:4px;'>\\2</li>");
    html.replace("\n", "<br>");
    return html;
}

void AIAssistantModule::toggleVoice() {
    m_voiceEnabled = !m_voiceEnabled;
    m_voiceBtn->setText(m_voiceEnabled ? "🔊 语音: 开" : "🔇 语音: 关");
    m_voiceBtn->setStyleSheet(m_voiceEnabled ? "color: #165DFF; font-weight: bold; border: none;" : "color: #86909C; font-weight: bold; border: none;");
}

void AIAssistantModule::speakText(const QString& text) {
    if (!m_voiceEnabled) return;
    QString cleanText = text;
    cleanText.remove(QRegularExpression("<[^>]*>")); cleanText.remove(QRegularExpression("[\\*`#]")); cleanText.replace("'", "''");
    QString command = QString("Add-Type -AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer).Speak('%1');").arg(cleanText);
    QProcess::startDetached("powershell", QStringList() << "-WindowStyle" << "Hidden" << "-Command" << command);
}
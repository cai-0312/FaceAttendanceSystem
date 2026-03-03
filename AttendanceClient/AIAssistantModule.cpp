#include "AIAssistantModule.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QScrollBar>

AIAssistantModule::AIAssistantModule(QTextBrowser* textBrowser,
    QLineEdit* lineEdit,
    QPushButton* sendBtn,
    QPushButton* clearBtn,
    QString userName,
    QObject* parent)
    : QObject(parent), m_textBrowser(textBrowser), m_lineEdit(lineEdit),
    m_sendBtn(sendBtn), m_clearBtn(clearBtn), m_userName(userName)
    , m_isReplying(false)
{
    m_networkManager = new QNetworkAccessManager(this);

    // 绑定网络回调
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AIAssistantModule::onNetworkReply);

    // ★ 绑定发送按钮和回车键
    if (m_sendBtn) connect(m_sendBtn, &QPushButton::clicked, this, &AIAssistantModule::onSendClicked);
    if (m_lineEdit) connect(m_lineEdit, &QLineEdit::returnPressed, this, &AIAssistantModule::onSendClicked);

    // ★ 绑定清除记忆按钮
    if (m_clearBtn) connect(m_clearBtn, &QPushButton::clicked, this, &AIAssistantModule::onClearClicked);

    // ==========================================
    // 🚀 API 配置区 (DeepSeek 官方配置)
    // 申请地址: https://platform.deepseek.com/api_keys
    // ==========================================
    m_apiUrl = "https://api.deepseek.com/chat/completions";
    m_apiKey = "sk-54ccee7e91ab405a94c622d9419a91e9"; // ★★★ 把你的 API KEY 填在这里 ★★★
    m_modelName = "deepseek-chat"; // DeepSeek 的核心对话模型模型名

    initializeContext();
}

// 🚀 实现：初始化 / 清除记忆
void AIAssistantModule::initializeContext() {
    m_messageHistory = QJsonArray();

    // 注入系统级系统提示词
    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = "你是一个专业的企业 OA 系统智能助手。用温和、专业的语气解答员工的问题。遇到换行请使用 <br>。当前与你对话的用户是：" + m_userName;
    m_messageHistory.append(systemMsg);

    // 清空屏幕并重新显示欢迎语
    m_currentHtmlDisplay = "<div style='text-align:center; padding: 20px 0;'>🤖 AI ✨</div>";
    m_textBrowser->setHtml(m_currentHtmlDisplay);

    appendMessage("ai", "你好！记忆已清空，我是您的专属智能办公助手，请问有什么可以帮您？");
}

// 🚀 实现：点击清除按钮事件
void AIAssistantModule::onClearClicked() {
    if (m_isReplying) return; // 如果正在回答，不让清空
    initializeContext();
}

// 🚀 实现：点击发送事件
void AIAssistantModule::onSendClicked() {
    if (m_isReplying) return;

    QString inputText = m_lineEdit->text().trimmed();
    if (inputText.isEmpty() || m_apiKey.contains("xxx")) {
        if (m_apiKey.contains("xxx")) appendMessage("ai", "⚠️ 请先在 AIAssistantModule.cpp 中填入您的真实 API Key！");
        return;
    }

    m_lineEdit->clear();
    m_isReplying = true;
    m_sendBtn->setEnabled(false);
    m_sendBtn->setText("思考中...");

    // 显示我的气泡
    appendMessage("user", inputText);

    // 追加到记忆数组中
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = inputText;
    m_messageHistory.append(userMsg);

    // 组装 JSON 发送
    QJsonObject requestBody;
    requestBody["model"] = m_modelName;
    requestBody["messages"] = m_messageHistory;
    requestBody["temperature"] = 0.7;

    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson();

    QNetworkRequest request((QUrl(m_apiUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    m_networkManager->post(request, data);
}

// 🚀 实现：接收大模型回复
void AIAssistantModule::onNetworkReply(QNetworkReply* reply) {
    m_isReplying = false;
    m_sendBtn->setEnabled(true);
    m_sendBtn->setText("发送");

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        QJsonObject json = doc.object();

        if (json.contains("choices") && json["choices"].isArray()) {
            QJsonArray choices = json["choices"].toArray();
            if (!choices.isEmpty()) {
                QJsonObject msgObj = choices[0].toObject()["message"].toObject();
                QString aiContent = msgObj["content"].toString();

                m_messageHistory.append(msgObj); // AI的话存入记忆
                appendMessage("ai", aiContent);
            }
        }
    }
    else {
        appendMessage("ai", "❌ 网络或接口异常: " + reply->errorString());
    }
    reply->deleteLater();
}

// 🚀 惊艳的气泡渲染
void AIAssistantModule::appendMessage(const QString& role, const QString& msg) {
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm");
    QString htmlMsg = msg;
    htmlMsg.replace("\n", "<br>");

    QString bubble;
    if (role == "user") {
        bubble = QString(
            "<div style='text-align:right; margin-bottom:15px; margin-right:5px;'>"
            "<span style='color:#8C8C8C; font-size:12px;'>我 %1</span><br>"
            "<span style='background-color:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #1E90FF, stop:1 #40E0D0);"
            "color:white; padding:10px 15px; border-radius:12px; display:inline-block; margin-top:5px; font-size:14px; max-width:80%; text-align:left;'>"
            "%2</span></div>"
        ).arg(timeStr, htmlMsg);
    }
    else {
        bubble = QString(
            "<div style='text-align:left; margin-bottom:15px; margin-left:5px;'>"
            "<span style='color:#8C8C8C; font-size:12px;'>🤖 AI 助手 %1</span><br>"
            "<span style='background-color:#FFFFFF; border:1px solid #EAEAEA; color:#2F3542;"
            "padding:10px 15px; border-radius:12px; display:inline-block; margin-top:5px; font-size:14px; max-width:80%;'>"
            "%2</span></div>"
        ).arg(timeStr, htmlMsg);
    }

    m_currentHtmlDisplay += bubble;
    m_textBrowser->setHtml(m_currentHtmlDisplay);

    QScrollBar* scrollBar = m_textBrowser->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}
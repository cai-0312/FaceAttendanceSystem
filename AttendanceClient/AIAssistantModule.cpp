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

    // 绑定网络请求完成的回调槽函数
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AIAssistantModule::onNetworkReply);

    // 绑定发送按钮点击与文本框回车事件
    if (m_sendBtn) connect(m_sendBtn, &QPushButton::clicked, this, &AIAssistantModule::onSendClicked);
    if (m_lineEdit) connect(m_lineEdit, &QLineEdit::returnPressed, this, &AIAssistantModule::onSendClicked);

    // 绑定清空对话历史按钮事件
    if (m_clearBtn) connect(m_clearBtn, &QPushButton::clicked, this, &AIAssistantModule::onClearClicked);

    // 接口配置：设置API端点、密钥及调用的模型名称
    m_apiUrl = "https://api.deepseek.com/chat/completions";
    m_apiKey = "sk-54ccee7e91ab405a94c622d9419a91e9";
    m_modelName = "deepseek-chat";

    // 初始化对话上下文
    initializeContext();
}

// 初始化上下文：构造系统提示词（System Prompt）并设置初始界面
void AIAssistantModule::initializeContext() {
    m_messageHistory = QJsonArray();

    // 注入系统预设角色指令，确保AI以专业助手身份回答
    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = "你是一个专业的企业 OA 系统智能助手。用温和、专业的语气解答员工的问题。遇到换行请使用 <br>。当前与你对话的用户是：" + m_userName;
    m_messageHistory.append(systemMsg);

    // 重置显示HTML内容并输出欢迎语
    m_currentHtmlDisplay = "<div style='text-align:center; padding: 20px 0;'>🤖 AI ✨</div>";
    m_textBrowser->setHtml(m_currentHtmlDisplay);

    appendMessage("ai", "你好！记忆已清空，我是您的专属智能办公助手，请问有什么可以帮您？");
}

// 槽函数：处理清空记忆点击，防止在回答过程中重置
void AIAssistantModule::onClearClicked() {
    if (m_isReplying) return;
    initializeContext();
}

// 发送逻辑：封装用户输入并向API发起异步网络请求
void AIAssistantModule::onSendClicked() {
    if (m_isReplying) return;

    QString inputText = m_lineEdit->text().trimmed();
    if (inputText.isEmpty() || m_apiKey.contains("xxx")) {
        if (m_apiKey.contains("xxx")) appendMessage("ai", "⚠️ 请先在 AIAssistantModule.cpp 中填入您的真实 API Key！");
        return;
    }

    // 状态切换：禁用输入防止连续重复点击
    m_lineEdit->clear();
    m_isReplying = true;
    m_sendBtn->setEnabled(false);
    m_sendBtn->setText("思考中...");

    // 在UI中渲染用户发送的消息气泡
    appendMessage("user", inputText);

    // 将用户输入压入对话历史上下文数组
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = inputText;
    m_messageHistory.append(userMsg);

    // 构造请求JSON报文
    QJsonObject requestBody;
    requestBody["model"] = m_modelName;
    requestBody["messages"] = m_messageHistory;
    requestBody["temperature"] = 0.7;

    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson();

    // 配置HTTP请求头与授权信息
    QNetworkRequest request((QUrl(m_apiUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    m_networkManager->post(request, data);
}

// 网络响应回调：解析API返回的JSON数据并提取AI回复内容
void AIAssistantModule::onNetworkReply(QNetworkReply* reply) {
    m_isReplying = false;
    m_sendBtn->setEnabled(true);
    m_sendBtn->setText("发送");

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        QJsonObject json = doc.object();

        // 路径解析：获取choices数组中首位消息的正文
        if (json.contains("choices") && json["choices"].isArray()) {
            QJsonArray choices = json["choices"].toArray();
            if (!choices.isEmpty()) {
                QJsonObject msgObj = choices[0].toObject()["message"].toObject();
                QString aiContent = msgObj["content"].toString();

                // 将AI回复存入历史记忆并渲染气泡
                m_messageHistory.append(msgObj);
                appendMessage("ai", aiContent);
            }
        }
    }
    else {
        // 错误处理：显示具体的网络异常信息
        appendMessage("ai", "❌ 网络或接口异常: " + reply->errorString());
    }
    reply->deleteLater();
}

// 消息气泡渲染：利用HTML/CSS在QTextBrowser中生成左右分布的对话框
void AIAssistantModule::appendMessage(const QString& role, const QString& msg) {
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm");
    QString htmlMsg = msg;
    htmlMsg.replace("\n", "<br>");

    QString bubble;
    if (role == "user") {
        // 用户气泡：蓝色渐变，右对齐
        bubble = QString(
            "<div style='text-align:right; margin-bottom:15px; margin-right:5px;'>"
            "<span style='color:#8C8C8C; font-size:12px;'>我 %1</span><br>"
            "<span style='background-color:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #1E90FF, stop:1 #40E0D0);"
            "color:white; padding:10px 15px; border-radius:12px; display:inline-block; margin-top:5px; font-size:14px; max-width:80%; text-align:left;'>"
            "%2</span></div>"
        ).arg(timeStr, htmlMsg);
    }
    else {
        // AI气泡：白色边框，左对齐
        bubble = QString(
            "<div style='text-align:left; margin-bottom:15px; margin-left:5px;'>"
            "<span style='color:#8C8C8C; font-size:12px;'>🤖 AI 助手 %1</span><br>"
            "<span style='background-color:#FFFFFF; border:1px solid #EAEAEA; color:#2F3542;"
            "padding:10px 15px; border-radius:12px; display:inline-block; margin-top:5px; font-size:14px; max-width:80%;'>"
            "%2</span></div>"
        ).arg(timeStr, htmlMsg);
    }

    // 更新显示内容并自动滚动到底部
    m_currentHtmlDisplay += bubble;
    m_textBrowser->setHtml(m_currentHtmlDisplay);

    QScrollBar* scrollBar = m_textBrowser->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}
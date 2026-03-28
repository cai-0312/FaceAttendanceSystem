#include "ConfigEncryptor.h"
#include <QFile>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QMessageAuthenticationCode>

// 生产环境建议引入 OpenSSL 或 QCA 库
QByteArray ConfigEncryptor::getEncryptionKey()
{
    // 从环境变量获取主密钥，不硬编码在程序中
    QString envKey = QProcessEnvironment::systemEnvironment().value("ATTENDANCE_CONFIG_KEY");
    if (envKey.isEmpty()) {
        qCritical() << "[ConfigEncryptor] FATAL: 环境变量 ATTENDANCE_CONFIG_KEY 未设置！";
        return QByteArray();
    }
    // 通过 SHA-256 派生固定长度的 AES 密钥
    return QCryptographicHash::hash(envKey.toUtf8(), QCryptographicHash::Sha256);
}

QJsonObject ConfigEncryptor::loadEncryptedConfig(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "[ConfigEncryptor] 无法打开配置文件:" << filePath;
        return QJsonObject();
    }
    QByteArray encrypted = file.readAll();
    file.close();

    QByteArray key = getEncryptionKey();
    if (key.isEmpty()) return QJsonObject();

    QByteArray decrypted = aesDecrypt(encrypted, key);
    QJsonDocument doc = QJsonDocument::fromJson(decrypted);
    return doc.object();
}

// AES 加解密的简化实现（生产环境应使用 OpenSSL EVP 接口）
// 此处使用 XOR + HMAC 作为演示框架，实际部署需替换为标准 AES-256-CBC
QByteArray ConfigEncryptor::aesEncrypt(const QByteArray& plaintext, const QByteArray& key)
{
    // 实际部署请替换为 OpenSSL AES-256-CBC
    // 以下为框架占位
    QByteArray result;
    for (int i = 0; i < plaintext.size(); ++i) {
        result.append(plaintext[i] ^ key[i % key.size()]);
    }
    // 附加 HMAC 完整性校验
    QByteArray hmac = QMessageAuthenticationCode::hash(
        result, key, QCryptographicHash::Sha256);
    return hmac + result;
}

QByteArray ConfigEncryptor::aesDecrypt(const QByteArray& ciphertext, const QByteArray& key)
{
    if (ciphertext.size() < 32) return QByteArray();
    QByteArray storedHmac = ciphertext.left(32);
    QByteArray data = ciphertext.mid(32);
    // 验证 HMAC 完整性
    QByteArray computedHmac = QMessageAuthenticationCode::hash(
        data, key, QCryptographicHash::Sha256);
    if (storedHmac != computedHmac) {
        qCritical() << "[ConfigEncryptor] 配置文件完整性校验失败！可能被篡改。";
        return QByteArray();
    }
    QByteArray result;
    for (int i = 0; i < data.size(); ++i) {
        result.append(data[i] ^ key[i % key.size()]);
    }
    return result;
}

bool ConfigEncryptor::saveEncryptedConfig(const QString& filePath, const QJsonObject& config)
{
    QByteArray key = getEncryptionKey();
    if (key.isEmpty()) return false;
    QByteArray json = QJsonDocument(config).toJson(QJsonDocument::Compact);
    QByteArray encrypted = aesEncrypt(json, key);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(encrypted);
    file.close();
    return true;
}
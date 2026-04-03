#include "ConfigEncryptor.h"
#include <QFile>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QMessageAuthenticationCode>
// 从外部环境变量派生并返回用于配置文件加密的密钥（固定长度）
QByteArray ConfigEncryptor::getEncryptionKey()
{
    // 从环境变量读取主密钥，避免将密钥硬编码在程序内
    QString envKey = QProcessEnvironment::systemEnvironment().value("ATTENDANCE_CONFIG_KEY");
    if (envKey.isEmpty()) {
        // 环境变量缺失视为致命错误，返回空字节表示不可用
        qCritical() << "[ConfigEncryptor] FATAL: 环境变量 ATTENDANCE_CONFIG_KEY 未设置！";
        return QByteArray();
    }
    // 使用 SHA-256 对外部密钥进行派生，得到固定长度的字节序列作为对称密钥
    return QCryptographicHash::hash(envKey.toUtf8(), QCryptographicHash::Sha256);
}
// 读取并解密配置文件，返回 JSON 对象
QJsonObject ConfigEncryptor::loadEncryptedConfig(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        // 无法打开文件则记录错误并返回空对象
        qCritical() << "[ConfigEncryptor] 无法打开配置文件:" << filePath;
        return QJsonObject();
    }
    // 读取整个文件内容（包含 HMAC + 数据）
    QByteArray encrypted = file.readAll();
    file.close();
    // 获取派生密钥用于解密与完整性校验
    QByteArray key = getEncryptionKey();
    if (key.isEmpty()) return QJsonObject();
    // 解密并解析为 JSON，如果解密或完整性校验失败将返回空对象
    QByteArray decrypted = aesDecrypt(encrypted, key);
    QJsonDocument doc = QJsonDocument::fromJson(decrypted);
    return doc.object();
}
// 简易的加密”实现对明文进行循环异或后附加 HMAC-SHA256 完整性校验
QByteArray ConfigEncryptor::aesEncrypt(const QByteArray& plaintext, const QByteArray& key)
{
    QByteArray result;
    // 对称简单异或（循环 key），将明文混淆
    for (int i = 0; i < plaintext.size(); ++i) {
        result.append(plaintext[i] ^ key[i % key.size()]);
    }
    // 计算 HMAC-SHA256 并将其前置，与数据一起写入文件以便后续完整性校验
    QByteArray hmac = QMessageAuthenticationCode::hash(
        result, key, QCryptographicHash::Sha256);
    return hmac + result;
}
// 对应的解密函数：验证前 32 字节 HMAC 后对剩余数据执行异或还原明文
QByteArray ConfigEncryptor::aesDecrypt(const QByteArray& ciphertext, const QByteArray& key)
{
    // 最少应包含 32 字节的 HMAC
    if (ciphertext.size() < 32) return QByteArray();
    QByteArray storedHmac = ciphertext.left(32);
    QByteArray data = ciphertext.mid(32);
    // 使用相同密钥计算 HMAC 并比对，若不一致则视为被篡改
    QByteArray computedHmac = QMessageAuthenticationCode::hash(
        data, key, QCryptographicHash::Sha256);
    if (storedHmac != computedHmac) {
        qCritical() << "[ConfigEncryptor] 配置文件完整性校验失败！可能被篡改。";
        return QByteArray();
    }
    // HMAC 校验通过后进行异或还原，得到原始明文
    QByteArray result;
    for (int i = 0; i < data.size(); ++i) {
        result.append(data[i] ^ key[i % key.size()]);
    }
    return result;
}
// 将 JSON 配置加密并保存到指定路径
bool ConfigEncryptor::saveEncryptedConfig(const QString& filePath, const QJsonObject& config)
{
    QByteArray key = getEncryptionKey();
    if (key.isEmpty()) return false;
    // 将 JSON 对象序列化为紧凑的字节流后加密
    QByteArray json = QJsonDocument(config).toJson(QJsonDocument::Compact);
    QByteArray encrypted = aesEncrypt(json, key);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(encrypted);
    file.close();
    return true;
}
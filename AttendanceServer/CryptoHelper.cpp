#include "CryptoHelper.h"
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QProcessEnvironment>
#include <QDebug>
// 生成指定长度的随机盐，用于密码哈希
QByteArray CryptoHelper::generateSalt(int length)
{
    QByteArray salt;
    salt.resize(length);
    QRandomGenerator* rng = QRandomGenerator::system();
    // 使用系统随机生成器填充盐字节
    for (int i = 0; i < length; ++i) {
        salt[i] = static_cast<char>(rng->bounded(256));
    }
    return salt;
}
// 使用 PBKDF2（HMAC-SHA256）派生密钥并返回指定长度的字节序列
QByteArray CryptoHelper::pbkdf2(const QByteArray& password, const QByteArray& salt,
    int iterations, int keyLength)
{
    QByteArray result;
    int blocksNeeded = (keyLength + 31) / 32;
    for (int block = 1; block <= blocksNeeded; ++block) {
        QByteArray saltWithBlock = salt;
        saltWithBlock.append(static_cast<char>((block >> 24) & 0xFF));
        saltWithBlock.append(static_cast<char>((block >> 16) & 0xFF));
        saltWithBlock.append(static_cast<char>((block >> 8) & 0xFF));
        saltWithBlock.append(static_cast<char>(block & 0xFF));
        // 计算 U1 并初始化 XOR 累计
        QByteArray u = QMessageAuthenticationCode::hash(
            saltWithBlock, password, QCryptographicHash::Sha256);
        QByteArray xorResult = u;
        // 迭代计算并对结果做 XOR 折叠
        for (int i = 1; i < iterations; ++i) {
            u = QMessageAuthenticationCode::hash(u, password, QCryptographicHash::Sha256);
            for (int j = 0; j < xorResult.size(); ++j) {
                xorResult[j] = xorResult[j] ^ u[j];
            }
        }
        result.append(xorResult);
    }
    return result.left(keyLength);
}
// 用 PBKDF2 对明文密码做哈希并返回带参数前缀的存储字符串
QString CryptoHelper::hashPassword(const QString& plainPassword)
{
    const int iterations = 100000;
    QByteArray salt = generateSalt(16);
    QByteArray hash = pbkdf2(plainPassword.toUtf8(), salt, iterations, 32);
    return QString("PBKDF2:%1:%2:%3")
        .arg(iterations)
        .arg(QString(salt.toHex()))
        .arg(QString(hash.toHex()));
}
// 验证明文或客户端发来的哈希与存储哈希是否匹配，兼容多种历史格式
bool CryptoHelper::verifyPassword(const QString& plainPassword, const QString& storedHash)
{
    // 处理 PBKDF2 新格式
    if (storedHash.startsWith("PBKDF2:")) {
        QStringList parts = storedHash.split(":");
        if (parts.size() != 4) return false;
        int iterations = parts[1].toInt();
        QByteArray salt = QByteArray::fromHex(parts[2].toUtf8());
        QByteArray expectedHash = QByteArray::fromHex(parts[3].toUtf8());
        if (iterations <= 0 || salt.isEmpty() || expectedHash.isEmpty()) return false;
        QByteArray computedHash = pbkdf2(plainPassword.toUtf8(), salt, iterations, 32);
        return computedHash == expectedHash;
    }
    // 兼容旧格式：直接比对客户端发来的哈希或明文的 SHA256
    if (storedHash == plainPassword) return true;
    QString sha256OfStored = QString(QCryptographicHash::hash(
        storedHash.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (sha256OfStored == plainPassword) return true;
    QString sha256OfPlain = QString(QCryptographicHash::hash(
        plainPassword.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (storedHash == sha256OfPlain) return true;

    return false;
}
// 从环境变量派生数据加密密钥，如未设置则使用回退密钥（仅开发用）
QByteArray CryptoHelper::getDataEncryptionKey()
{
    QString envKey = QProcessEnvironment::systemEnvironment()
        .value("ATTENDANCE_DATA_KEY");
    if (envKey.isEmpty()) {
        qWarning() << "[CryptoHelper] 警告: ATTENDANCE_DATA_KEY 未设置，使用内置回退密钥（仅限开发）";
        envKey = "FaceAttendance_Dev_FallbackKey_2026";
    }
    return QCryptographicHash::hash(envKey.toUtf8(), QCryptographicHash::Sha256);
}
// 使用 HMAC-CTR 风格的流式异或对称加密并返回带前缀的 Base64 字符串
QString CryptoHelper::encryptContent(const QString& plaintext)
{
    QByteArray key = getDataEncryptionKey();
    QByteArray iv = generateSalt(16);
    QByteArray data = plaintext.toUtf8();
    QByteArray encrypted;
    encrypted.resize(data.size());
    const int blockSize = 32;
    for (int i = 0; i < data.size(); i += blockSize) {
        // 生成计数器并用 HMAC-SHA256 得到密钥流
        QByteArray counter = iv + QByteArray::number(i / blockSize);
        QByteArray keyStream = QMessageAuthenticationCode::hash(
            counter, key, QCryptographicHash::Sha256);

        int chunkLen = qMin(blockSize, data.size() - i);
        for (int j = 0; j < chunkLen; ++j) {
            encrypted[i + j] = data[i + j] ^ keyStream[j];
        }
    }
    QByteArray combined = iv + encrypted;
    return "AES256:" + QString(combined.toBase64());
}
// 对 AES256: 前缀的数据按 CTR 样式还原并返回明文
QString CryptoHelper::decryptContent(const QString& ciphertext)
{
    if (!ciphertext.startsWith("AES256:")) return ciphertext;
    QByteArray key = getDataEncryptionKey();
    QByteArray combined = QByteArray::fromBase64(ciphertext.mid(7).toUtf8());
    if (combined.size() < 17) {
        qWarning() << "[CryptoHelper] 解密失败：数据太短";
        return "[解密失败:数据损坏]";
    }
    QByteArray iv = combined.left(16);
    QByteArray data = combined.mid(16);
    QByteArray decrypted;
    decrypted.resize(data.size());
    const int blockSize = 32;
    for (int i = 0; i < data.size(); i += blockSize) {
        QByteArray counter = iv + QByteArray::number(i / blockSize);
        QByteArray keyStream = QMessageAuthenticationCode::hash(
            counter, key, QCryptographicHash::Sha256);
        int chunkLen = qMin(blockSize, data.size() - i);
        for (int j = 0; j < chunkLen; ++j) {
            decrypted[i + j] = data[i + j] ^ keyStream[j];
        }
    }
    return QString::fromUtf8(decrypted);
}
// 自动识别并解码 AES256 / B64 / 明文，返回明文字符串
QString CryptoHelper::safeDecrypt(const QString& stored)
{
    if (stored.startsWith("AES256:")) return decryptContent(stored);
    if (stored.startsWith("B64:"))
        return QString::fromUtf8(QByteArray::fromBase64(stored.mid(4).toUtf8()));
    return stored;
}

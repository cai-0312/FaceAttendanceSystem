#include "CryptoHelper.h"
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QProcessEnvironment>
#include <QDebug>

QByteArray CryptoHelper::generateSalt(int length)
{
    QByteArray salt;
    salt.resize(length);
    QRandomGenerator* rng = QRandomGenerator::system();
    for (int i = 0; i < length; ++i) {
        salt[i] = static_cast<char>(rng->bounded(256));
    }
    return salt;
}

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

        QByteArray u = QMessageAuthenticationCode::hash(
            saltWithBlock, password, QCryptographicHash::Sha256);
        QByteArray xorResult = u;

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
bool CryptoHelper::verifyPassword(const QString& plainPassword, const QString& storedHash)
{
    // ── 新格式：PBKDF2 ──
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

    // ── 旧格式兼容（以下 plainPassword 是客户端发来的 SHA-256 哈希值）──

    // 情况 A：数据库存的也是 SHA-256 哈希 → 直接比对
    if (storedHash == plainPassword) return true;

    // 情况 B：数据库存的是明文密码 → 对明文做 SHA-256 后与客户端发来的比对
    // 这是你当前数据库的实际情况：stored="qwer1234", pwd=SHA256("qwer1234")
    QString sha256OfStored = QString(QCryptographicHash::hash(
        storedHash.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (sha256OfStored == plainPassword) return true;

    // 情况 C：数据库存的是 MD5 哈希，客户端发的也是 MD5 → 直接比对
    // （已在情况 A 中覆盖）

    // 情况 D：数据库存的是明文，客户端发的也是明文（非标准流程，保底兼容）
    QString sha256OfPlain = QString(QCryptographicHash::hash(
        plainPassword.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (storedHash == sha256OfPlain) return true;

    return false;
}

// ═══════════════════════════════════════════════════════════════════
//  对称加密（双向可逆） —— HMAC-CTR 流加密模式
// ═══════════════════════════════════════════════════════════════════

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

// CTR 模式加密：keyStream = HMAC(iv + counter, key)，然后 XOR 明文
// XOR 是自逆运算，所以加密和解密用完全相同的逻辑
QString CryptoHelper::encryptContent(const QString& plaintext)
{
    QByteArray key = getDataEncryptionKey();
    QByteArray iv = generateSalt(16);       // 16 字节随机 IV
    QByteArray data = plaintext.toUtf8();

    // CTR 模式加密
    QByteArray encrypted;
    encrypted.resize(data.size());

    const int blockSize = 32;  // HMAC-SHA256 输出 32 字节
    for (int i = 0; i < data.size(); i += blockSize) {
        // 计数器 = IV 拼接块索引
        QByteArray counter = iv + QByteArray::number(i / blockSize);
        // 生成密钥流
        QByteArray keyStream = QMessageAuthenticationCode::hash(
            counter, key, QCryptographicHash::Sha256);

        int chunkLen = qMin(blockSize, data.size() - i);
        for (int j = 0; j < chunkLen; ++j) {
            encrypted[i + j] = data[i + j] ^ keyStream[j];
        }
    }

    // 格式: AES256:Base64(IV + 密文)
    QByteArray combined = iv + encrypted;
    return "AES256:" + QString(combined.toBase64());
}

// CTR 模式解密：与加密完全对称（XOR 自逆）
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

    // CTR 模式解密（与加密逻辑完全相同）
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

// 智能解码：自动识别 AES256 加密 / 旧 B64 编码 / 明文
QString CryptoHelper::safeDecrypt(const QString& stored)
{
    if (stored.startsWith("AES256:")) return decryptContent(stored);
    if (stored.startsWith("B64:"))
        return QString::fromUtf8(QByteArray::fromBase64(stored.mid(4).toUtf8()));
    return stored;
}
#ifndef CRYPTO_HELPER_H
#define CRYPTO_HELPER_H
#include <QString>
#include <QByteArray>
class CryptoHelper
{
public:
    static QString hashPassword(const QString& plainPassword);                                // 对明文密码进行哈希处理并返回安全存储字符串
    static bool verifyPassword(const QString& plainPassword, const QString& storedHash);      // 验证明文密码是否与存储哈希匹配
    static QString encryptContent(const QString& plaintext);                                  // 使用对称密钥加密文本并返回编码后的密文
    static QString decryptContent(const QString& ciphertext);                                 // 解密并返回明文字符串
    static QString safeDecrypt(const QString& stored);                                        // 安全解密兼容老版本存储格式的内容
private:
    static QByteArray getDataEncryptionKey();                                                 // 获取或派生用于数据加密的对称密钥
    static QByteArray generateSalt(int length = 16);                                         // 生成指定长度的随机盐值
    static QByteArray pbkdf2(const QByteArray& password, const QByteArray& salt,
        int iterations = 100000, int keyLength = 32);                                       // 使用 PBKDF2 从密码派生密钥
};
#endif // CRYPTO_HELPER_H
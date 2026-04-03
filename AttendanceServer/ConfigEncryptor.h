#ifndef CONFIG_ENCRYPTOR_H
#define CONFIG_ENCRYPTOR_H
#include <QString>
#include <QJsonObject>
class ConfigEncryptor
{
public:
    static QJsonObject loadEncryptedConfig(const QString& filePath); // 从加密配置文件加载并返回解析的 JSON 配置
    static bool saveEncryptedConfig(const QString& filePath, const QJsonObject& config); // 将 JSON 配置加密并写入文件
private:
    static QByteArray getEncryptionKey(); // 获取或派生用于配置文件加解密的对称密钥
    static QByteArray aesEncrypt(const QByteArray& plaintext, const QByteArray& key); // 使用 AES 对明文进行加密并返回密文
    static QByteArray aesDecrypt(const QByteArray& ciphertext, const QByteArray& key); // 使用 AES 对密文进行解密并返回明文
};
#endif // CONFIG_ENCRYPTOR_H
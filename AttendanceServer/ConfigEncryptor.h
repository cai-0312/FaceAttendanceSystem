#ifndef CONFIG_ENCRYPTOR_H
#define CONFIG_ENCRYPTOR_H

#include <QString>
#include <QJsonObject>

class ConfigEncryptor
{
public:
    // 从加密配置文件读取数据库连接信息
    static QJsonObject loadEncryptedConfig(const QString& filePath);
    // 生成加密配置文件
    static bool saveEncryptedConfig(const QString& filePath, const QJsonObject& config);
private:
    static QByteArray getEncryptionKey();
    static QByteArray aesEncrypt(const QByteArray& plaintext, const QByteArray& key);
    static QByteArray aesDecrypt(const QByteArray& ciphertext, const QByteArray& key);
};

#endif // CONFIG_ENCRYPTOR_H
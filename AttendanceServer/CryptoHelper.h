#ifndef CRYPTO_HELPER_H
#define CRYPTO_HELPER_H
#include <QString>
#include <QByteArray>

class CryptoHelper
{
public:
    static QString hashPassword(const QString& plainPassword);
    static bool verifyPassword(const QString& plainPassword, const QString& storedHash);
    static QString encryptContent(const QString& plaintext);
    static QString decryptContent(const QString& ciphertext);
    static QString safeDecrypt(const QString& stored);
private:
    static QByteArray getDataEncryptionKey();
    static QByteArray generateSalt(int length = 16);
    static QByteArray pbkdf2(const QByteArray& password, const QByteArray& salt,
        int iterations = 100000, int keyLength = 32);
};
#endif // CRYPTO_HELPER_H
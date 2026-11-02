#include "pch.h"
/// @file SecretCompare.cpp
/// @brief Constant-time compare and PBKDF2 web password hashes.

#include "crypto/SecretCompare.h"

#include <QByteArray>
#include <QStringList>

#include <algorithm>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace eMule {

namespace {

constexpr int kSaltBytes = 16;
constexpr int kHashBytes = 32;
constexpr int kMinIterations = 1;
constexpr int kMaxIterations = 10'000'000;   // stored value is untrusted
const QString kPrefix = QStringLiteral("pbkdf2");

[[nodiscard]] QByteArray pbkdf2(const QByteArray& password, const QByteArray& salt, int iterations)
{
    QByteArray out(kHashBytes, Qt::Uninitialized);
    if (PKCS5_PBKDF2_HMAC(password.constData(), static_cast<int>(password.size()),
                          reinterpret_cast<const unsigned char*>(salt.constData()),
                          static_cast<int>(salt.size()), iterations, EVP_sha256(),
                          kHashBytes, reinterpret_cast<unsigned char*>(out.data())) != 1)
        return {};
    return out;
}

[[nodiscard]] bool isHex(const QString& s)
{
    return std::ranges::all_of(s, [](QChar c) {
        return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
    });
}

} // namespace

bool constantTimeEquals(QByteArrayView a, QByteArrayView b)
{
    if (a.size() != b.size())
        return false;
    if (a.isEmpty())
        return true;
    return CRYPTO_memcmp(a.data(), b.data(), static_cast<size_t>(a.size())) == 0;
}

QString hashWebPassword(const QString& sha256Hex, int iterations)
{
    if (sha256Hex.isEmpty())
        return {};
    iterations = std::clamp(iterations, kMinIterations, kMaxIterations);

    QByteArray salt(kSaltBytes, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(salt.data()), kSaltBytes) != 1)
        return {};
    const QByteArray hash = pbkdf2(sha256Hex.toLower().toLatin1(), salt, iterations);
    if (hash.isEmpty())
        return {};
    return QStringLiteral("%1$%2$%3$%4").arg(kPrefix).arg(iterations)
        .arg(QString::fromLatin1(salt.toHex()), QString::fromLatin1(hash.toHex()));
}

bool verifyWebPassword(const QString& sha256Hex, const QString& stored)
{
    if (stored.isEmpty() || sha256Hex.isEmpty())
        return false;

    if (isLegacyWebPasswordHash(stored))
        return constantTimeEquals(sha256Hex.toLower().toLatin1(), stored.toLower().toLatin1());

    const QStringList parts = stored.split(QLatin1Char('$'));
    if (parts.size() != 4 || parts.at(0) != kPrefix)
        return false;
    bool ok = false;
    const int iterations = parts.at(1).toInt(&ok);
    if (!ok || iterations < kMinIterations || iterations > kMaxIterations)
        return false;
    const QByteArray salt = QByteArray::fromHex(parts.at(2).toLatin1());
    const QByteArray want = QByteArray::fromHex(parts.at(3).toLatin1());
    if (salt.isEmpty() || want.size() != kHashBytes)
        return false;

    const QByteArray got = pbkdf2(sha256Hex.toLower().toLatin1(), salt, iterations);
    return !got.isEmpty() && constantTimeEquals(got, want);
}

bool isLegacyWebPasswordHash(const QString& stored)
{
    return stored.size() == 64 && isHex(stored);
}

QString upgradeWebPasswordHash(const QString& stored)
{
    return isLegacyWebPasswordHash(stored) ? hashWebPassword(stored) : stored;
}

} // namespace eMule

/// @file tst_SecretCompare.cpp
/// @brief Tests for crypto/SecretCompare — constant-time compare, web password hashes.

#include "TestHelpers.h"
#include "crypto/SecretCompare.h"

#include <QCryptographicHash>
#include <QTest>

using namespace eMule;

namespace {

QString sha256Hex(const QString& text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

constexpr int kCheapIterations = 1000;   // keeps the test fast

} // namespace

class tst_SecretCompare : public QObject {
    Q_OBJECT

private slots:
    void constantTimeEquals_comparesBytes()
    {
        QVERIFY(constantTimeEquals("abc", "abc"));
        QVERIFY(constantTimeEquals(QByteArrayView(), QByteArrayView()));
        QVERIFY(!constantTimeEquals("abc", "abd"));
        QVERIFY(!constantTimeEquals("abc", "abcd"));   // length mismatch
        QVERIFY(!constantTimeEquals("", "a"));
    }

    void hash_roundTrip()
    {
        const QString pw = sha256Hex(QStringLiteral("secret"));
        const QString stored = hashWebPassword(pw, kCheapIterations);
        QVERIFY(stored.startsWith(QStringLiteral("pbkdf2$1000$")));
        QCOMPARE(stored.split(QLatin1Char('$')).size(), 4);
        QVERIFY(!stored.contains(pw));
        QVERIFY(verifyWebPassword(pw, stored));
        // Upper-case hex is the same password
        QVERIFY(verifyWebPassword(pw.toUpper(), stored));
        QVERIFY(!isLegacyWebPasswordHash(stored));

        // A fresh salt every time
        QVERIFY(hashWebPassword(pw, kCheapIterations) != stored);
        QVERIFY(hashWebPassword(QString()).isEmpty());
    }

    void hash_defaultIterations()
    {
        const QString pw = sha256Hex(QStringLiteral("secret"));
        const QString stored = hashWebPassword(pw);
        QVERIFY(stored.startsWith(QStringLiteral("pbkdf2$%1$").arg(kWebPasswordIterations)));
        QVERIFY(verifyWebPassword(pw, stored));
    }

    void verify_wrongPasswordFails()
    {
        const QString stored = hashWebPassword(sha256Hex(QStringLiteral("secret")), kCheapIterations);
        QVERIFY(!verifyWebPassword(sha256Hex(QStringLiteral("Secret")), stored));
        QVERIFY(!verifyWebPassword(QString(), stored));
        QVERIFY(!verifyWebPassword(sha256Hex(QStringLiteral("secret")), QString()));

        // Malformed stored values match nothing
        QVERIFY(!verifyWebPassword(sha256Hex(QStringLiteral("secret")), QStringLiteral("pbkdf2$x$00$00")));
        QVERIFY(!verifyWebPassword(sha256Hex(QStringLiteral("secret")), QStringLiteral("pbkdf2$1000$$")));
        QVERIFY(!verifyWebPassword(sha256Hex(QStringLiteral("secret")), QStringLiteral("md5$1$aa$bb")));
        QVERIFY(!verifyWebPassword(sha256Hex(QStringLiteral("secret")), QStringLiteral("0123456789abcdef")));
    }

    void legacy_comparesConstantTime()
    {
        const QString legacy = sha256Hex(QStringLiteral("secret"));
        QVERIFY(isLegacyWebPasswordHash(legacy));
        QVERIFY(verifyWebPassword(legacy, legacy));
        QVERIFY(verifyWebPassword(legacy, legacy.toUpper()));
        QVERIFY(!verifyWebPassword(sha256Hex(QStringLiteral("other")), legacy));
        QVERIFY(!isLegacyWebPasswordHash(legacy.left(63)));
        QVERIFY(!isLegacyWebPasswordHash(legacy.left(63) + QLatin1Char('g')));
    }

    void upgrade_wrapsOnlyLegacy()
    {
        const QString legacy = sha256Hex(QStringLiteral("secret"));
        const QString wrapped = upgradeWebPasswordHash(legacy);
        QVERIFY(wrapped.startsWith(QStringLiteral("pbkdf2$")));
        QVERIFY(verifyWebPassword(legacy, wrapped));
        QCOMPARE(upgradeWebPasswordHash(wrapped), wrapped);
        QCOMPARE(upgradeWebPasswordHash(QString()), QString());
    }
};

QTEST_MAIN(tst_SecretCompare)
#include "tst_SecretCompare.moc"

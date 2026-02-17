#include <QTest>
#include "crypto/MD5Hash.h"
#include "utils/OtherFunctions.h"

#include <cstring>

using namespace eMule;

class tst_MD5 : public QObject {
    Q_OBJECT

private slots:
    void rfc1321_empty();
    void rfc1321_a();
    void rfc1321_abc();
    void rfc1321_messageDigest();
    void rfc1321_alphabet();
    void constructFromString();
    void constructFromData();
    void getHashStringFormat();
};

// RFC 1321 test vectors (lowercase hex)
// MD5("") = d41d8cd98f00b204e9800998ecf8427e
// MD5("a") = 0cc175b9c0f1b6a831c399e269772661
// MD5("abc") = 900150983cd24fb0d6963f7d28e17f72
// MD5("message digest") = f96b697d7cb7938d525a2f31aaf161d0
// MD5("abcdefghijklmnopqrstuvwxyz") = c3fcd3d76192e4007dfb496cca67e13b

void tst_MD5::rfc1321_empty()
{
    MD5Hasher h;
    h.calculate(static_cast<const uint8*>(nullptr), 0);
    QCOMPARE(h.getHashString(), u"d41d8cd98f00b204e9800998ecf8427e");
}

void tst_MD5::rfc1321_a()
{
    MD5Hasher h;
    h.calculate(reinterpret_cast<const uint8*>("a"), 1);
    QCOMPARE(h.getHashString(), u"0cc175b9c0f1b6a831c399e269772661");
}

void tst_MD5::rfc1321_abc()
{
    MD5Hasher h;
    h.calculate(reinterpret_cast<const uint8*>("abc"), 3);
    QCOMPARE(h.getHashString(), u"900150983cd24fb0d6963f7d28e17f72");
}

void tst_MD5::rfc1321_messageDigest()
{
    MD5Hasher h;
    h.calculate(reinterpret_cast<const uint8*>("message digest"), 14);
    QCOMPARE(h.getHashString(), u"f96b697d7cb7938d525a2f31aaf161d0");
}

void tst_MD5::rfc1321_alphabet()
{
    MD5Hasher h;
    h.calculate(reinterpret_cast<const uint8*>("abcdefghijklmnopqrstuvwxyz"), 26);
    QCOMPARE(h.getHashString(), u"c3fcd3d76192e4007dfb496cca67e13b");
}

void tst_MD5::constructFromString()
{
    MD5Hasher h(QStringLiteral("abc"));
    QCOMPARE(h.getHashString(), u"900150983cd24fb0d6963f7d28e17f72");
}

void tst_MD5::constructFromData()
{
    const auto* data = reinterpret_cast<const uint8*>("abc");
    MD5Hasher h(data, 3);
    QCOMPARE(h.getHashString(), u"900150983cd24fb0d6963f7d28e17f72");
}

void tst_MD5::getHashStringFormat()
{
    MD5Hasher h;
    h.calculate(reinterpret_cast<const uint8*>(""), 0);
    const QString str = h.getHashString();
    // Must be 32 lowercase hex characters
    QCOMPARE(str.length(), 32);
    for (QChar c : str) {
        QVERIFY(c.isDigit() || (c >= u'a' && c <= u'f'));
    }
}

QTEST_MAIN(tst_MD5)
#include "tst_MD5.moc"

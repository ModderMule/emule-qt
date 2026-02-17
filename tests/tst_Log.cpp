/// @file tst_Log.cpp
/// @brief Tests for Log.h — LogFile, convenience logging functions.

#include "TestHelpers.h"

#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/MapKey.h"
#include "utils/Exceptions.h"
#include "utils/PerfLog.h"

#include <QFile>
#include <QTest>
#include <QTextStream>

using namespace eMule;

class tst_Log : public QObject {
    Q_OBJECT

private slots:
    // LogFile tests
    void logFile_createAndWrite();
    void logFile_rotation();
    void logFile_reopenAppends();

    // Opcodes compile-time checks
    void opcodes_partsize();
    void opcodes_timeMacros();
    void opcodes_protocolHeaders();

    // MapKey tests
    void hashKeyRef_equality();
    void hashKeyOwn_equality();
    void hashKeyOwn_defaultZero();

    // Exceptions tests
    void emuleException_what();
    void clientException_shouldDelete();
    void ioException_thrown();

    // PerfLog tests
    void perfLog_uninitializedNoOp();
};

// ---------------------------------------------------------------------------
// LogFile
// ---------------------------------------------------------------------------

void tst_Log::logFile_createAndWrite()
{
    eMule::testing::TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("test.log"));

    LogFile lf;
    QVERIFY(lf.create(path));
    QVERIFY(lf.isOpen());
    QVERIFY(lf.log(QStringLiteral("Hello from test")));

    // Verify file contains the message
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QTextStream(&f).readAll();
    QVERIFY(content.contains(QStringLiteral("Hello from test")));
}

void tst_Log::logFile_rotation()
{
    eMule::testing::TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("rotate.log"));

    LogFile lf;
    QVERIFY(lf.create(path, 256));  // very small max size to trigger rotation

    // Write enough data to trigger rotation
    for (int i = 0; i < 20; ++i)
        lf.log(QStringLiteral("Line %1 - padding data to fill").arg(i));

    // The original file should still exist (reopened after rotation)
    QVERIFY(lf.isOpen());
    QVERIFY(QFile::exists(path));
}

void tst_Log::logFile_reopenAppends()
{
    eMule::testing::TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("append.log"));

    {
        LogFile lf;
        QVERIFY(lf.create(path));
        lf.log(QStringLiteral("First"));
    }

    {
        LogFile lf;
        QVERIFY(lf.create(path));
        lf.log(QStringLiteral("Second"));
    }

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QTextStream(&f).readAll();
    QVERIFY(content.contains(QStringLiteral("First")));
    QVERIFY(content.contains(QStringLiteral("Second")));
}

// ---------------------------------------------------------------------------
// Opcodes compile-time checks
// ---------------------------------------------------------------------------

void tst_Log::opcodes_partsize()
{
    // PARTSIZE must be uint64 and match 9728000
    static_assert(PARTSIZE == UINT64_C(9728000));
    static_assert(MAX_EMULE_FILE_SIZE == UINT64_C(0x4000000000));
    QVERIFY(true);
}

void tst_Log::opcodes_timeMacros()
{
    static_assert(SEC(1) == 1);
    static_assert(MIN2S(1) == 60);
    static_assert(HR2S(1) == 3600);
    static_assert(DAY2S(1) == 86400);
    static_assert(SEC2MS(1) == 1000);
    static_assert(MIN2MS(1) == 60000);
    QVERIFY(true);
}

void tst_Log::opcodes_protocolHeaders()
{
    static_assert(OP_EDONKEYHEADER == 0xE3);
    static_assert(OP_KADEMLIAHEADER == 0xE4);
    static_assert(OP_EMULEPROT == 0xC5);
    static_assert(UNLIMITED == UINT32_MAX);
    QVERIFY(true);
}

// ---------------------------------------------------------------------------
// MapKey
// ---------------------------------------------------------------------------

void tst_Log::hashKeyRef_equality()
{
    const std::array<uint8, 16> h1 = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    const std::array<uint8, 16> h2 = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    std::array<uint8, 16> h3 = {};

    HashKeyRef a(h1.data());
    HashKeyRef b(h2.data());
    HashKeyRef c(h3.data());

    QVERIFY(a == b);
    QVERIFY(!(a == c));
}

void tst_Log::hashKeyOwn_equality()
{
    const std::array<uint8, 16> h = {0xDE,0xAD,0xBE,0xEF, 1,2,3,4, 5,6,7,8, 9,10,11,12};
    HashKeyOwn a(h.data());
    HashKeyOwn b(h.data());
    HashKeyOwn c; // zero

    QVERIFY(a == b);
    QVERIFY(!(a == c));
}

void tst_Log::hashKeyOwn_defaultZero()
{
    HashKeyOwn key;
    const std::array<uint8, 16> zero = {};
    QVERIFY(md4equ(key.data(), zero.data()));
}

// ---------------------------------------------------------------------------
// Exceptions
// ---------------------------------------------------------------------------

void tst_Log::emuleException_what()
{
    try {
        throw EmuleException("test error");
    } catch (const std::runtime_error& e) {
        QCOMPARE(std::string(e.what()), std::string("test error"));
        return;
    }
    QFAIL("Exception not caught");
}

void tst_Log::clientException_shouldDelete()
{
    ClientException ex("client error", true);
    QVERIFY(ex.shouldDelete());
    QCOMPARE(std::string(ex.what()), std::string("client error"));

    ClientException ex2("no delete", false);
    QVERIFY(!ex2.shouldDelete());
}

void tst_Log::ioException_thrown()
{
    QVERIFY_THROWS_EXCEPTION(IOException, throw IOException("io error"));
}

// ---------------------------------------------------------------------------
// PerfLog
// ---------------------------------------------------------------------------

void tst_Log::perfLog_uninitializedNoOp()
{
    PerfLog pl;
    // Should be a no-op when not initialized
    pl.logSamples(100, 200, 10, 20);
    pl.shutdown();
    QVERIFY(true);
}

QTEST_MAIN(tst_Log)
#include "tst_Log.moc"

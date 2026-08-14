/// @file tst_Log.cpp
/// @brief Tests for Log.h — LogFile, convenience logging functions.

#include "TestHelpers.h"

#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/MapKey.h"
#include "utils/Exceptions.h"
#include "utils/PerfLog.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTest>
#include <QTextStream>

using namespace eMule;

class tst_Log : public QObject {
    Q_OBJECT

private slots:
    void cleanup();

    // LogFile tests
    void logFile_createAndWrite();
    void logFile_rotation();
    void logFile_reopenAppends();
    void logFile_timestampHasMilliseconds();
    void logFile_rotationKeepsSingleBak();

    // Console message pattern
    void consoleMessagePattern_formatsTimeTagAndCategory();

    // Log file sink
    void logFileSink_splitsDebugIntoVerboseFile();
    void logFileSink_routesKadToItsOwnFile();
    void logFileSink_disabledWritesNothing();
    void logFileSink_messageHandlerFeedsSink();

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

private:
    /// Whole contents of @p path, or an empty string if it does not exist.
    static QString readAll(const QString& path);
};

void tst_Log::cleanup()
{
    // Both are process-wide state: leaving either set would leak into the next
    // test (and, for the pattern, into every later qDebug in this binary).
    qSetMessagePattern(QString());
    closeLogFileSink();
}

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

// The file carries milliseconds so its lines can be lined up against the
// console output, which uses HH:mm:ss.zzz (installConsoleMessagePattern).
void tst_Log::logFile_timestampHasMilliseconds()
{
    eMule::testing::TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("ms.log"));

    LogFile lf;
    QVERIFY(lf.create(path));
    QVERIFY(lf.log(QStringLiteral("payload")));

    const QString content = readAll(path);
    const QRegularExpression re(
        QStringLiteral(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}: payload$)"));
    QVERIFY2(re.match(content.trimmed()).hasMatch(), qPrintable(content));
}

// Only one generation is kept, unlike the reference, which names each backup
// after the time the log started and so never deletes any.
void tst_Log::logFile_rotationKeepsSingleBak()
{
    eMule::testing::TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("roll.log"));

    LogFile lf;
    QVERIFY(lf.create(path));   // rotate explicitly, not by size
    lf.log(QStringLiteral("generation one"));
    lf.startNewLogFile();
    lf.log(QStringLiteral("generation two"));
    lf.startNewLogFile();
    lf.log(QStringLiteral("generation three"));

    // The .bak holds the generation before the current one; the oldest is gone.
    QVERIFY(readAll(path).contains(QStringLiteral("generation three")));
    QVERIFY(readAll(path + QStringLiteral(".bak")).contains(QStringLiteral("generation two")));

    const QStringList produced = QDir(tmp.path()).entryList(QDir::Files);
    QCOMPARE(produced.size(), 2);
}

// ---------------------------------------------------------------------------
// Console message pattern
// ---------------------------------------------------------------------------

void tst_Log::consoleMessagePattern_formatsTimeTagAndCategory()
{
    installConsoleMessagePattern(QStringLiteral("core"));

    const QMessageLogContext ctx(__FILE__, __LINE__, Q_FUNC_INFO, "emule.general");
    const QString formatted = qFormatLogMessage(QtInfoMsg, ctx, QStringLiteral("hello"));

    const QRegularExpression re(
        QStringLiteral(R"(^\d{2}:\d{2}:\d{2}\.\d{3} \[core\] emule\.general: hello$)"));
    QVERIFY2(re.match(formatted).hasMatch(), qPrintable(formatted));

    // "gui" is a character shorter — padding keeps the category column aligned.
    installConsoleMessagePattern(QStringLiteral("gui"));
    QVERIFY(qFormatLogMessage(QtInfoMsg, ctx, QStringLiteral("hello"))
                .contains(QStringLiteral("[gui ] emule.general: hello")));
}

// ---------------------------------------------------------------------------
// Log file sink
// ---------------------------------------------------------------------------

void tst_Log::logFileSink_splitsDebugIntoVerboseFile()
{
    eMule::testing::TempDir tmp;
    applyLogFileSink(tmp.path(), QStringLiteral("emuleqt"), true, 1048576);

    writeToLogFileSink(QtInfoMsg, "emule.general", QStringLiteral("an info line"));
    writeToLogFileSink(QtWarningMsg, "emule.general", QStringLiteral("a warning line"));
    writeToLogFileSink(QtDebugMsg, "emule.net", QStringLiteral("a debug line"));
    closeLogFileSink();

    const QString main = readAll(tmp.filePath(QStringLiteral("emuleqt.log")));
    const QString verbose = readAll(tmp.filePath(QStringLiteral("emuleqt_Verbose.log")));
    const QString kad = readAll(tmp.filePath(QStringLiteral("emuleqt_Kad.log")));

    // Debug is the reference's LOG_DEBUG: verbose file only, never the main one.
    QVERIFY(main.contains(QStringLiteral("emule.general: an info line")));
    QVERIFY(main.contains(QStringLiteral("emule.general: a warning line")));
    QVERIFY(!main.contains(QStringLiteral("a debug line")));

    QVERIFY(verbose.contains(QStringLiteral("emule.net: a debug line")));
    QVERIFY(!verbose.contains(QStringLiteral("an info line")));

    // Only emule.kad reaches the Kad file — a non-Kad debug line must not.
    QVERIFY(kad.isEmpty());
}

// Kad is routed by category, not by severity, so that the file holds the same
// lines as the GUI's Kad tab and the verbose log is left readable.
void tst_Log::logFileSink_routesKadToItsOwnFile()
{
    eMule::testing::TempDir tmp;
    applyLogFileSink(tmp.path(), QStringLiteral("emulecored"), true, 1048576);

    writeToLogFileSink(QtDebugMsg, "emule.kad", QStringLiteral("a kad line"));
    writeToLogFileSink(QtWarningMsg, "emule.kad", QStringLiteral("a kad warning"));
    writeToLogFileSink(QtDebugMsg, "emule.net", QStringLiteral("a net line"));
    closeLogFileSink();

    const QString main = readAll(tmp.filePath(QStringLiteral("emulecored.log")));
    const QString verbose = readAll(tmp.filePath(QStringLiteral("emulecored_Verbose.log")));
    const QString kad = readAll(tmp.filePath(QStringLiteral("emulecored_Kad.log")));

    QVERIFY(kad.contains(QStringLiteral("emule.kad: a kad line")));
    // Category beats severity: a non-debug Kad line stays out of the main file.
    QVERIFY(kad.contains(QStringLiteral("emule.kad: a kad warning")));
    QVERIFY(!main.contains(QStringLiteral("a kad warning")));

    QVERIFY(!verbose.contains(QStringLiteral("a kad line")));
    QVERIFY(verbose.contains(QStringLiteral("emule.net: a net line")));
    QVERIFY(!kad.contains(QStringLiteral("a net line")));
}

void tst_Log::logFileSink_disabledWritesNothing()
{
    eMule::testing::TempDir tmp;
    applyLogFileSink(tmp.path(), QStringLiteral("emulecored"), false, 1048576);

    writeToLogFileSink(QtInfoMsg, "emule.general", QStringLiteral("dropped"));
    writeToLogFileSink(QtDebugMsg, "emule.kad", QStringLiteral("dropped"));

    // Not merely empty — a disabled sink must not create the files at all.
    QCOMPARE(QDir(tmp.path()).entryList(QDir::Files).size(), 0);
}

// The handler is what carries real log calls into the sink. It is installed at
// the top of main() in both binaries, so it also catches startup lines emitted
// before the daemon's forwarder or the GUI's LogWidget exist.
void tst_Log::logFileSink_messageHandlerFeedsSink()
{
    eMule::testing::TempDir tmp;
    installLogFileMessageHandler();   // idempotent — safe to repeat across tests
    applyLogFileSink(tmp.path(), QStringLiteral("emulecored"), true, 1048576);

    logInfo(QStringLiteral("routed through the handler"));
    closeLogFileSink();

    QVERIFY(readAll(tmp.filePath(QStringLiteral("emulecored.log")))
                .contains(QStringLiteral("emule.general: routed through the handler")));
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

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

QString tst_Log::readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QTextStream(&f).readAll();
}

QTEST_MAIN(tst_Log)
#include "tst_Log.moc"

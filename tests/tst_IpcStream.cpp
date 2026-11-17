/// @file tst_IpcStream.cpp
/// @brief StatStream / ReadStream / GetIpcIdentity (IpcStreamBridge): file bytes over IPC
/// for the qDonkey phone app.

#include "TestHelpers.h"

#include "IpcStreamBridge.h"

#include "app/AppContext.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"

#include <QCborMap>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <optional>

using namespace eMule;
using Ipc::IpcMessage;
using Ipc::IpcMsgType;

class tst_IpcStream : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void statSharedFile();
    void readRanges();
    void readPastEnd();
    void unknownId();
    void otherTypesNotHandled();
    void identityAnswers();

private:
    /// Runs one request, waits for its reply
    std::optional<IpcMessage> ask(IpcMessage msg);

    QTemporaryDir m_dir;
    KnownFileList* m_known = nullptr;
    SharedFileList* m_shared = nullptr;
    QByteArray m_content;
    QString m_hashHex;
};

void tst_IpcStream::initTestCase()
{
    QVERIFY(m_dir.isValid());
    thePrefs.load(m_dir.filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_dir.path());

    m_content.resize(300 * 1024);
    for (qsizetype i = 0; i < m_content.size(); ++i)
        m_content[i] = static_cast<char>(i * 7 + 3);
    const QString path = m_dir.filePath(QStringLiteral("clip.bin"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(m_content), m_content.size());
    f.close();

    m_known = new KnownFileList();
    theApp.knownFileList = m_known;
    m_shared = new SharedFileList(m_known, this);
    theApp.sharedFileList = m_shared;

    uint8 hash[16];
    std::memset(hash, 0x5A, sizeof(hash));
    auto* kf = new KnownFile();
    kf->setFileHash(hash);
    kf->setFileName(QStringLiteral("clip.bin"));
    kf->setFileSize(static_cast<uint64>(m_content.size()));
    kf->setFilePath(path);
    QVERIFY(m_shared->safeAddKFile(kf));
    m_hashHex = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(hash), 16).toHex());
}

void tst_IpcStream::cleanupTestCase()
{
    theApp.sharedFileList = nullptr;
    theApp.knownFileList = nullptr;
}

std::optional<IpcMessage> tst_IpcStream::ask(IpcMessage msg)
{
    std::optional<IpcMessage> got;
    const bool handled = IpcStreamBridge::handleRequest(msg.withSeqId(7), this,
                                                        [&got](const IpcMessage& r) { got = r; });
    if (!handled)
        return std::nullopt;
    // QTRY_* returns void on failure: wait by hand
    QElapsedTimer clock;
    clock.start();
    while (!got && clock.elapsed() < 5000)
        QTest::qWait(10);
    return got;
}

void tst_IpcStream::statSharedFile()
{
    IpcMessage msg(IpcMsgType::StatStream);
    msg.append(m_hashHex);
    const auto r = ask(msg);
    QVERIFY(r);
    QCOMPARE(r->seqId(), 7);
    QVERIFY(r->fieldBool(0));
    const QCborMap m = r->fieldMap(1);
    QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("clip.bin"));
    QCOMPARE(m.value(QStringLiteral("size")).toInteger(), m_content.size());
    QVERIFY(m.value(QStringLiteral("complete")).toBool());
    QVERIFY(!m.value(QStringLiteral("seekable")).toBool());
}

void tst_IpcStream::readRanges()
{
    // a middle chunk, then one capped at kMaxChunk
    IpcMessage msg(IpcMsgType::ReadStream);
    msg.append(m_hashHex);
    msg.append(qint64(1000));
    msg.append(qint64(4096));
    auto r = ask(msg);
    QVERIFY(r && r->fieldBool(0));
    QCOMPARE(r->fieldMap(1).value(QStringLiteral("data")).toByteArray(), m_content.mid(1000, 4096));

    IpcMessage big(IpcMsgType::ReadStream);
    big.append(m_hashHex);
    big.append(qint64(0));
    big.append(qint64(64) * 1024 * 1024);
    r = ask(big);
    QVERIFY(r && r->fieldBool(0));
    // whole file is below the cap: all of it
    QCOMPARE(r->fieldMap(1).value(QStringLiteral("data")).toByteArray(), m_content);
}

void tst_IpcStream::readPastEnd()
{
    IpcMessage msg(IpcMsgType::ReadStream);
    msg.append(m_hashHex);
    msg.append(qint64(m_content.size() + 10));
    msg.append(qint64(100));
    const auto r = ask(msg);
    QVERIFY(r && r->fieldBool(0));
    QVERIFY(r->fieldMap(1).value(QStringLiteral("data")).toByteArray().isEmpty());
    QCOMPARE(r->fieldMap(1).value(QStringLiteral("size")).toInteger(), m_content.size());
}

void tst_IpcStream::unknownId()
{
    IpcMessage msg(IpcMsgType::StatStream);
    msg.append(QStringLiteral("00112233445566778899aabbccddeeff"));
    auto r = ask(msg);
    QVERIFY(r);
    QVERIFY(!r->fieldBool(0));
    QVERIFY(!r->fieldMap(1).value(QStringLiteral("error")).toString().isEmpty());

    IpcMessage bad(IpcMsgType::ReadStream);
    bad.append(QStringLiteral("nothex"));
    bad.append(qint64(0));
    bad.append(qint64(10));
    r = ask(bad);
    QVERIFY(r);
    QVERIFY(!r->fieldBool(0));
}

void tst_IpcStream::otherTypesNotHandled()
{
    QVERIFY(!ask(IpcMessage(IpcMsgType::GetStats)).has_value());
}

void tst_IpcStream::identityAnswers()
{
    // no DaemonApp here: no fingerprint, still an answer with the port
    const auto r = ask(IpcMessage(IpcMsgType::GetIpcIdentity));
    QVERIFY(r && r->fieldBool(0));
    QVERIFY(r->fieldMap(1).contains(QStringLiteral("fingerprint")));
    QCOMPARE(r->fieldMap(1).value(QStringLiteral("port")).toInteger(), qint64(thePrefs.ipcPort()));
}

QTEST_GUILESS_MAIN(tst_IpcStream)
#include "tst_IpcStream.moc"

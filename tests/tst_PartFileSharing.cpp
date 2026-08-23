/// @file tst_PartFileSharing.cpp
/// @brief Part files are shared files — MFC srchybrid/DownloadQueue.cpp:109,127.
///
/// Until this was wired up, an in-progress download reached SharedFileList only when
/// it *finished*. Both advertising paths — the ED2K server's OP_OFFERFILES list and Kad
/// source publishing — walk that map and nothing else, so a client downloading a file
/// never told anybody it held part of it. Peers could still be served (findUploadFile()
/// falls back to the download queue), but only if they had already learnt about us some
/// other way; we were invisible to discovery.
///
/// The gate is MFC's: the full MD4 hashset plus at least one verified complete part.
/// Deliberately not the PartFileStatus enum — see the comment on PartFile::canBeShared().

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "crypto/FileIdentifier.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "net/Address.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "utils/Opcodes.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <cstring>
#include <memory>

using namespace eMule;

namespace {

/// One whole part plus a short tail, so filling part 0 never empties the gap list and
/// the file does not wander off into completeFile() halfway through a test.
///
/// Not an exact multiple of PARTSIZE, and that matters: eMule expects
/// `size / PARTSIZE + 1` part hashes (FileIdentifier::getTheoreticalMD4PartHashCount),
/// the trailing zero-length chunk included, so a file of exactly 2*PARTSIZE wants
/// *three* hashes and hasExpectedMD4HashCount() stays false with two — which would make
/// canBeShared() answer no for a reason that has nothing to do with what is being tested.
constexpr uint64 kFileSize = PARTSIZE + 1000;

const Address kPeerAddress = Address::fromString(QStringLiteral("87.65.43.21"));

/// The eD2K file hash of a hash set: MD4 over the concatenated part hashes.
std::array<uint8, 16> fileHashOf(const std::vector<std::array<uint8, 16>>& parts)
{
    QByteArray buffer;
    for (const auto& part : parts)
        buffer.append(reinterpret_cast<const char*>(part.data()), 16);

    const QByteArray digest = QCryptographicHash::hash(buffer, QCryptographicHash::Md4);
    std::array<uint8, 16> out{};
    std::memcpy(out.data(), digest.constData(), out.size());
    return out;
}

QByteArray pattern(qsizetype size, quint8 salt)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        out[i] = static_cast<char>((i * 131 + (i >> 11) * 17 + salt) & 0xFF);
    return out;
}

} // namespace

class tst_PartFileSharing : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void canBeShared_needsTheHashset();
    void canBeShared_needsACompletePart();
    void verifiedPartSharesTheFile();
    void sharingIsIdempotentAcrossFlushes();
    void sharedPartFileSurvivesReload();
    void leavingTheQueueUnsharesWithoutPoisoningTheHash();

    // Rehash on load (MFC PS_WAITINGFORHASH)
    void changedPartFileIsRehashedAndLosesTheBadPart();
    void untouchedPartFileIsNotRehashed();

private:
    /// A part file whose part 0 hash is real, so hashSinglePart() has something to
    /// compare against. Owned by m_file and torn down by cleanup(), which runs even
    /// when a QCOMPARE fails and returns early — the queue would otherwise be left
    /// holding a pointer to a file the test had already destroyed.
    PartFile* makeFile(const QString& name);
    void writeGoodPart(PartFile& file);

    QTemporaryDir m_dir;
    QString m_tempDir;
    std::unique_ptr<KnownFileList> m_knownFiles;
    std::unique_ptr<SharedFileList> m_shared;
    std::unique_ptr<DownloadQueue> m_queue;
    std::unique_ptr<PartFile> m_file;

    QByteArray m_goodPart;
    std::array<uint8, 16> m_goodPartHash{};
    int m_serial = 0;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void tst_PartFileSharing::initTestCase()
{
    QVERIFY(m_dir.isValid());

    thePrefs.load(m_dir.filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_dir.path());

    m_tempDir = m_dir.filePath(QStringLiteral("temp"));
    QDir().mkpath(m_tempDir);
    thePrefs.setTempDirs({m_tempDir});

    // Nothing may be scanned into the shared list from disk: every file that ends up
    // in the map during these tests must have got there through addToSharedFiles(),
    // or the assertions prove nothing.
    const QString incoming = m_dir.filePath(QStringLiteral("incoming"));
    QDir().mkpath(incoming);
    thePrefs.setIncomingDir(incoming);
    thePrefs.setSharedDirs({});

    m_goodPart = pattern(static_cast<qsizetype>(PARTSIZE), 0);
    const QByteArray digest = QCryptographicHash::hash(m_goodPart, QCryptographicHash::Md4);
    std::memcpy(m_goodPartHash.data(), digest.constData(), m_goodPartHash.size());
}

void tst_PartFileSharing::cleanupTestCase()
{
    theApp.downloadQueue = nullptr;
    theApp.sharedFileList = nullptr;
    theApp.knownFileList = nullptr;
}

void tst_PartFileSharing::init()
{
    m_knownFiles = std::make_unique<KnownFileList>();
    m_shared = std::make_unique<SharedFileList>(m_knownFiles.get());
    m_queue = std::make_unique<DownloadQueue>();

    theApp.knownFileList = m_knownFiles.get();
    theApp.sharedFileList = m_shared.get();
    theApp.downloadQueue = m_queue.get();

    m_queue->setSharedFileList(m_shared.get());
    m_queue->setKnownFileList(m_knownFiles.get());
}

void tst_PartFileSharing::cleanup()
{
    // Unhook before anything is destroyed, and in this order. QCOMPARE returns from the
    // test function on failure, so nothing a test does at its own end can be relied on
    // to run; only this can.
    if (m_file) {
        if (m_queue)
            m_queue->removeFile(m_file.get());
        if (m_shared)
            m_shared->removeFile(m_file.get());
    }

    // Queue first: its deleteAll() reaches into SharedFileList to unshare part files.
    m_queue.reset();
    m_shared.reset();
    m_knownFiles.reset();
    m_file.reset();
}

// ---------------------------------------------------------------------------
// The gate
// ---------------------------------------------------------------------------

void tst_PartFileSharing::canBeShared_needsTheHashset()
{
    PartFile file;
    file.setFileName(QStringLiteral("no-hashset.bin"));
    file.setFileSize(EMFileSize(kFileSize));
    file.setTmpPath(m_tempDir);

    std::array<uint8, 16> hash{};
    hash.fill(0x5A);
    file.setFileHash(hash.data());
    QVERIFY(file.createPartFile(m_tempDir));

    // Without the hashset there is nothing to verify a part against, so anything we
    // handed out would be bytes we could not vouch for.
    QVERIFY(file.isMD4HashsetNeeded());
    QVERIFY2(!file.canBeShared(), "a part file with no MD4 hashset must not be shared");

    file.addToSharedFiles();
    QCOMPARE(m_shared->getCount(), 0);
}

void tst_PartFileSharing::canBeShared_needsACompletePart()
{
    PartFile* file = makeFile(QStringLiteral("no-parts"));
    QVERIFY(file);

    // Hashset present, but not one byte written yet.
    QVERIFY(file->fileIdentifier().hasExpectedMD4HashCount());
    QVERIFY2(!file->canBeShared(),
             "a part file with no complete part has nothing to serve");

    file->addToSharedFiles();
    QCOMPARE(m_shared->getCount(), 0);
}

// ---------------------------------------------------------------------------
// The happy path
// ---------------------------------------------------------------------------

void tst_PartFileSharing::verifiedPartSharesTheFile()
{
    PartFile* file = makeFile(QStringLiteral("one-good-part"));
    QVERIFY(file);
    QVERIFY2(file->fileIdentifier().getMD4PartHash(0) != nullptr,
             "no part hash to verify against — the part would pass whatever it held");

    QCOMPARE(m_shared->getCount(), 0);

    writeGoodPart(*file);
    file->flushBuffer();

    QVERIFY2(file->isComplete(0), "part 0 should have verified");
    QVERIFY(file->canBeShared());

    // This is the whole point: the file is now advertisable, and it is still a part
    // file, so the server offer list writes the 0xFCFCFCFC magic for it and Kad source
    // publishing walks it.
    QCOMPARE(m_shared->getCount(), 1);
    QCOMPARE(m_shared->getFileByID(file->fileHash()), static_cast<KnownFile*>(file));
    QVERIFY(m_shared->getFileByID(file->fileHash())->isPartFile());
}

void tst_PartFileSharing::sharingIsIdempotentAcrossFlushes()
{
    PartFile* file = makeFile(QStringLiteral("idempotent"));
    QVERIFY(file);

    writeGoodPart(*file);

    // flushBuffer() re-verifies every already complete part on every flush, not just
    // the ones this buffer touched, so the share hook fires again and again.
    for (int i = 0; i < 5; ++i)
        file->flushBuffer();

    QCOMPARE(m_shared->getCount(), 1);
}

void tst_PartFileSharing::sharedPartFileSurvivesReload()
{
    PartFile* file = makeFile(QStringLiteral("survives-reload"));
    QVERIFY(file);

    writeGoodPart(*file);
    file->flushBuffer();
    QCOMPARE(m_shared->getCount(), 1);

    // The file has to be in the queue for the re-add to find it — reload() clears the
    // whole map, and a rescan of the shared directories cannot bring a part file back
    // because the scan skips *.part outright.
    m_queue->addDownload(file);

    m_shared->reload();

    QCOMPARE(m_shared->getCount(), 1);
    QCOMPARE(m_shared->getFileByID(file->fileHash()), static_cast<KnownFile*>(file));
}

void tst_PartFileSharing::leavingTheQueueUnsharesWithoutPoisoningTheHash()
{
    PartFile* file = makeFile(QStringLiteral("cancelled"));
    QVERIFY(file);

    writeGoodPart(*file);
    file->flushBuffer();
    m_queue->addDownload(file);
    QCOMPARE(m_shared->getCount(), 1);

    m_queue->removeFile(file);

    // Gone from the map — otherwise the next server offer or Kad publish would read a
    // pointer to an object the caller is about to delete.
    QCOMPARE(m_shared->getCount(), 0);

    // Recorded as a hash we used to share, which is what lets isUnsharedFile() tell a
    // requesting peer we know the file but are not offering it
    // (MFC RemoveFile, srchybrid/SharedFileList.cpp:776 -> BaseClient.cpp:2540).
    QVERIFY2(m_shared->isUnsharedFile(file->fileHash()),
             "dropping a file must record the hash for isUnsharedFile()");

    // The mark is bookkeeping, not a gate. Re-adding the same file must work — a
    // cancelled download the user restarts has to be shareable again — and the add
    // clears the mark (MFC AddFile, srchybrid/SharedFileList.cpp:695).
    QVERIFY2(m_shared->safeAddKFile(file),
             "the same file must be shareable again after being dropped from the queue");
    QCOMPARE(m_shared->getCount(), 1);
    QVERIFY2(!m_shared->isUnsharedFile(file->fileHash()),
             "re-adding a file must clear the unshared mark");
}

// ---------------------------------------------------------------------------
// Rehash on load
// ---------------------------------------------------------------------------

void tst_PartFileSharing::changedPartFileIsRehashedAndLosesTheBadPart()
{
    PartFile* file = makeFile(QStringLiteral("touched"));
    QVERIFY(file);

    writeGoodPart(*file);
    file->flushBuffer();
    QVERIFY2(file->isComplete(0u), "part 0 must be verified before we corrupt it");
    QVERIFY(file->savePartFile());

    const QString metName = QFileInfo(file->fullName()).fileName();
    const QString partPath = file->partDataPath();

    // Let go of the object, as a restart would.
    m_shared->removeFile(file);
    m_file.reset();

    // Something else wrote to the .part — an unclean shutdown, a stray tool. The bytes
    // no longer match what the .part.met says is there.
    //
    // The modification time is set explicitly rather than left to the write: mtime has
    // one-second resolution, and the save that stamped the .part.met happened in the
    // same second, so the write alone would leave the two dates equal. That is the
    // detector's real blind spot and not what this test is about.
    {
        QFile part(partPath);
        QVERIFY(part.open(QIODevice::ReadWrite));
        QVERIFY(part.seek(0));
        QCOMPARE(part.write(QByteArray(4096, '\xEE')), qint64{4096});
        part.close();
    }
    {
        // Separately, and after the close: closing flushes the write, and that flush
        // stamps the modification time again — setting it on the still-open handle
        // would simply be overwritten.
        QFile part(partPath);
        QVERIFY(part.open(QIODevice::ReadWrite));
        QVERIFY(part.setFileTime(QDateTime::currentDateTime().addSecs(-3600),
                                 QFileDevice::FileModificationTime));
        part.close();
    }
    QCOMPARE(QFileInfo(partPath).lastModified().toSecsSinceEpoch(),
             QDateTime::currentDateTime().addSecs(-3600).toSecsSinceEpoch());

    auto reloaded = std::make_unique<PartFile>();
    QCOMPARE(reloaded->loadPartFile(m_tempDir, metName), PartFileLoadResult::LoadSuccess);

    // Not Ready, not Empty: the file is off-limits until it has been re-verified, which
    // is exactly what keeps DownloadQueue::process() and the upload paths off it.
    QVERIFY2(reloaded->status() == PartFileStatus::Hashing
                 || reloaded->status() == PartFileStatus::WaitingForHash,
             "a .part whose date disagrees with the .part.met must be rehashed");

    // The completion routes back through the download queue, so it has to be findable.
    m_file = std::move(reloaded);
    m_queue->addDownload(m_file.get());

    QVERIFY2(QTest::qWaitFor([&] { return m_file->status(true) != PartFileStatus::Hashing
                                      && m_file->status(true) != PartFileStatus::WaitingForHash; },
                             10000),
             "the rehash must finish and move the file out of Hashing");

    // Part 0's bytes changed, so it must come back as missing rather than being trusted.
    QVERIFY2(!m_file->isComplete(0u), "a part that no longer hashes must become a gap again");
    QCOMPARE(m_file->status(true), PartFileStatus::Empty);
    QVERIFY2(!m_shared->getFileByID(m_file->fileHash()),
             "a file with nothing verified must not be offered");
}

void tst_PartFileSharing::untouchedPartFileIsNotRehashed()
{
    PartFile* file = makeFile(QStringLiteral("intact"));
    QVERIFY(file);

    writeGoodPart(*file);
    file->flushBuffer();
    QVERIFY(file->savePartFile());

    const QString metName = QFileInfo(file->fullName()).fileName();
    m_shared->removeFile(file);
    m_file.reset();

    // Nothing touched the .part, so savePartFile()'s stamp still matches it and there
    // is no reason to read the whole file again on every startup.
    auto reloaded = std::make_unique<PartFile>();
    QCOMPARE(reloaded->loadPartFile(m_tempDir, metName), PartFileLoadResult::LoadSuccess);

    QVERIFY2(reloaded->status() != PartFileStatus::Hashing
                 && reloaded->status() != PartFileStatus::WaitingForHash,
             "an untouched part file must load without rehashing");
    // And it loads straight into the shareable latch, because part 0 is still verified.
    QCOMPARE(reloaded->status(true), PartFileStatus::Ready);
    QVERIFY(reloaded->isComplete(0u));

    m_file = std::move(reloaded);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

PartFile* tst_PartFileSharing::makeFile(const QString& name)
{
    auto file = std::make_unique<PartFile>();

    file->setFileName(name + QStringLiteral(".bin"));
    file->setFileSize(EMFileSize(kFileSize));
    file->setTmpPath(m_tempDir);

    // Only part 0's hash has to be real; part 1 is never written, so it is never
    // hashed. Varying it is what gives each case a distinct file hash.
    std::array<uint8, 16> unusedHash{};
    unusedHash.fill(0x22);
    unusedHash[0] = static_cast<uint8>(++m_serial);
    const std::vector<std::array<uint8, 16>> partHashes{m_goodPartHash, unusedHash};

    // setMD4HashSet() verifies the set against the file hash and discards it when they
    // disagree, which would leave hashSinglePart() with nothing to compare against and
    // every part looking fine.
    file->setFileHash(fileHashOf(partHashes).data());
    if (!file->fileIdentifier().setMD4HashSet(partHashes))
        return nullptr;

    // Loudly, because a silent mismatch here would make every canBeShared() assertion
    // in this file pass or fail for the wrong reason.
    if (!file->fileIdentifier().hasExpectedMD4HashCount())
        return nullptr;

    if (!file->createPartFile(m_tempDir))
        return nullptr;

    m_file = std::move(file);
    return m_file.get();
}

void tst_PartFileSharing::writeGoodPart(PartFile& file)
{
    // In block-sized pieces, the way a real transfer arrives — the corruption blackbox
    // refuses a single range as large as a whole part.
    for (qsizetype pos = 0; pos < m_goodPart.size(); pos += EMBLOCKSIZE) {
        const qsizetype len = std::min<qsizetype>(EMBLOCKSIZE, m_goodPart.size() - pos);
        const auto start = static_cast<uint64>(pos);
        file.writeToBuffer(static_cast<uint64>(len),
                           reinterpret_cast<const uint8*>(m_goodPart.constData() + pos),
                           start, start + static_cast<uint64>(len) - 1,
                           nullptr, kPeerAddress);
    }
}

QTEST_MAIN(tst_PartFileSharing)
#include "tst_PartFileSharing.moc"

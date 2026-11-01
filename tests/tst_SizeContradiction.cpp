/// @file tst_SizeContradiction.cpp
/// @brief A part-aligned download size whose hashset belongs to a bigger file
/// (ed2k-server #40).
///
/// Size n × PARTSIZE expects n + 1 part hashes, as many as any file of up to
/// (n + 1) × PARTSIZE - 1 bytes. So the real file's hashset passes the count and root
/// checks, and only its last hash shows it: MD4("") for the link's size, real data for
/// the real file. Covers the check, the hold, the size fix and the lookup coordinator.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "crypto/FileIdentifier.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "files/PartFileSizeLookup.h"
#include "files/SharedFileList.h"
#include "kademlia/KadSearch.h"
#include "kademlia/Kademlia.h"
#include "net/Address.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "transfer/DownloadQueue.h"
#include "utils/Opcodes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <cstring>
#include <memory>

using namespace eMule;

namespace {

using Hash = std::array<uint8, 16>;

constexpr uint64 kWrongSize = 2 * PARTSIZE;           // what the link says
constexpr uint64 kRealSize = 2 * PARTSIZE + 1000;     // what the hashset is for

const Address kPeerAddress = Address::fromString(QStringLiteral("87.65.43.21"));

Hash md4(const QByteArray& data)
{
    const QByteArray digest = QCryptographicHash::hash(data, QCryptographicHash::Md4);
    Hash out{};
    std::memcpy(out.data(), digest.constData(), out.size());
    return out;
}

Hash rootOf(const std::vector<Hash>& parts)
{
    QByteArray buffer;
    for (const auto& part : parts)
        buffer.append(reinterpret_cast<const char*>(part.data()), 16);
    return md4(buffer);
}

QByteArray pattern(qsizetype size, quint8 salt)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        out[i] = static_cast<char>((i * 131 + (i >> 11) * 17 + salt) & 0xFF);
    return out;
}

QByteArray hashBytes(uint8 fill)
{
    return QByteArray(16, static_cast<char>(fill));
}

} // namespace

class tst_SizeContradiction : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // The check
    void emptyMd4Constant();
    void reportedSizesAreTheLowerBound();
    void tailContradictsPartAlignedSize();
    void realSizeIsFine();
    void partAlignedRealFileIsFine();
    void smallFileIsFine();

    // The download
    void heldWhenHashsetContradicts();
    void forgedHashsetProvesNothing();
    void correctedSizeKeepsDoneParts();
    void correctionOutsideRangeIsIgnored();
    void failedLookupPausesAndResumeRetries();

    // The coordinator
    void serverAnswerWinsAndStopsKad();
    void kadAnswersWhenServerFails();
    void bothFailReportsBothReasons();
    void outOfRangeAnswerIsAFailure();
    void disabledNetworkIsNotAsked();
    void bothDisabledFailsWithoutAsking();
    void queuedLookupsRunInTurn();

    // Kad size probe
    void kadProbeTakesMostCommonSize();
    void kadProbeFinishesOnTwoAgreeingSources();
    void kadProbeAddsNoSources();

private:
    PartFile* makeWrongSizeFile(const QString& name, bool forgeRoot = false);
    void writeGoodPart(PartFile& file);

    struct Calls {
        int serverStarts = 0;
        int kadStarts = 0;
        int kadCancels = 0;
        int serverCancels = 0;
        QList<std::pair<QByteArray, uint64>> applied;
        QString appliedFrom;
        QList<QByteArray> failed;
        QString serverReason;
        QString kadReason;
    };
    PartFileSizeLookup::Backend stubBackend(Calls& calls, PartFileSizeLookup::Net server,
                                            PartFileSizeLookup::Net kad);

    QTemporaryDir m_dir;
    QString m_tempDir;
    std::unique_ptr<KnownFileList> m_knownFiles;
    std::unique_ptr<SharedFileList> m_shared;
    std::unique_ptr<DownloadQueue> m_queue;
    std::unique_ptr<PartFile> m_file;

    QByteArray m_goodPart;
    Hash m_goodPartHash{};
    int m_serial = 0;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void tst_SizeContradiction::initTestCase()
{
    QVERIFY(m_dir.isValid());
    thePrefs.load(m_dir.filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_dir.path());
    m_tempDir = m_dir.filePath(QStringLiteral("temp"));
    QDir().mkpath(m_tempDir);
    thePrefs.setTempDirs({m_tempDir});
    const QString incoming = m_dir.filePath(QStringLiteral("incoming"));
    QDir().mkpath(incoming);
    thePrefs.setIncomingDir(incoming);
    thePrefs.setSharedDirs({});

    m_goodPart = pattern(static_cast<qsizetype>(PARTSIZE), 0);
    m_goodPartHash = md4(m_goodPart);
}

void tst_SizeContradiction::init()
{
    m_knownFiles = std::make_unique<KnownFileList>();
    m_shared = std::make_unique<SharedFileList>(m_knownFiles.get());
    m_queue = std::make_unique<DownloadQueue>();
    theApp.knownFileList = m_knownFiles.get();
    theApp.sharedFileList = m_shared.get();
    theApp.downloadQueue = m_queue.get();
    theApp.partFileSizeLookup = nullptr;
    m_queue->setSharedFileList(m_shared.get());
    m_queue->setKnownFileList(m_knownFiles.get());
}

void tst_SizeContradiction::cleanup()
{
    if (m_file && m_shared)
        m_shared->removeFile(m_file.get());
    m_queue.reset();
    m_shared.reset();
    m_knownFiles.reset();
    m_file.reset();
    theApp.downloadQueue = nullptr;
    theApp.sharedFileList = nullptr;
    theApp.knownFileList = nullptr;
}

// ---------------------------------------------------------------------------
// The check
// ---------------------------------------------------------------------------

void tst_SizeContradiction::emptyMd4Constant()
{
    QCOMPARE(FileIdentifier::kEmptyMD4, md4(QByteArray()));
}

void tst_SizeContradiction::reportedSizesAreTheLowerBound()
{
    // Link size / real size of the three files in the report
    const std::array<std::pair<uint64, uint64>, 3> samples{{
        {1731584000, 1737984263},
        {1644032000, 1650712159},
        {457216000, 458070039},
    }};
    for (const auto& [link, real] : samples) {
        QCOMPARE(link % PARTSIZE, uint64{0});
        EMFileSize linkSize(link);
        EMFileSize realSize(real);
        FileIdentifier linkId(linkSize);
        FileIdentifier realId(realSize);
        // Why nothing noticed: the same hash count
        QCOMPARE(linkId.getTheoreticalMD4PartHashCount(), realId.getTheoreticalMD4PartHashCount());
        // And the real size lies in the range the lookup accepts
        QVERIFY(real > link && real < link + PARTSIZE);
    }
}

void tst_SizeContradiction::tailContradictsPartAlignedSize()
{
    EMFileSize size(kWrongSize);
    FileIdentifier id(size);
    const std::vector<Hash> parts{m_goodPartHash, md4("b"), md4("tail")};
    id.getRawMD4HashSet() = parts;   // root isn't this check's business
    QVERIFY(id.hasExpectedMD4HashCount());
    QVERIFY(id.md4TailContradictsSize());
}

void tst_SizeContradiction::realSizeIsFine()
{
    EMFileSize size(kRealSize);
    FileIdentifier id(size);
    id.getRawMD4HashSet() = {m_goodPartHash, md4("b"), md4("tail")};
    QVERIFY(id.hasExpectedMD4HashCount());
    QVERIFY(!id.md4TailContradictsSize());
}

void tst_SizeContradiction::partAlignedRealFileIsFine()
{
    EMFileSize size(kWrongSize);
    FileIdentifier id(size);
    id.getRawMD4HashSet() = {m_goodPartHash, md4("b"), FileIdentifier::kEmptyMD4};
    QVERIFY(!id.md4TailContradictsSize());
}

void tst_SizeContradiction::smallFileIsFine()
{
    EMFileSize size(PARTSIZE - 1);
    FileIdentifier id(size);
    QVERIFY(!id.md4TailContradictsSize());
    EMFileSize zero(0);
    FileIdentifier none(zero);
    QVERIFY(!none.md4TailContradictsSize());
}

// ---------------------------------------------------------------------------
// The download
// ---------------------------------------------------------------------------

void tst_SizeContradiction::heldWhenHashsetContradicts()
{
    PartFile* file = makeWrongSizeFile(QStringLiteral("held"));
    QVERIFY(file);
    QVERIFY(file->checkSizeAgainstHashset());
    QVERIFY(file->isSizeContradicted());

    // A verified part would normally share it; the bad size must not get out
    writeGoodPart(*file);
    file->flushBuffer();
    QVERIFY(file->isComplete(0));
    QCOMPARE(m_shared->getCount(), 0);
    QVERIFY(!file->isPaused());   // held, not paused, while the lookup runs
}

void tst_SizeContradiction::forgedHashsetProvesNothing()
{
    // Root doesn't match the file hash: says nothing about the size
    PartFile* file = makeWrongSizeFile(QStringLiteral("forged"), /*forgeRoot*/ true);
    QVERIFY(file);
    QVERIFY(file->fileIdentifier().md4TailContradictsSize());
    QVERIFY(!file->checkSizeAgainstHashset());
    QVERIFY(!file->isSizeContradicted());
}

void tst_SizeContradiction::correctedSizeKeepsDoneParts()
{
    PartFile* file = makeWrongSizeFile(QStringLiteral("corrected"));
    QVERIFY(file);
    writeGoodPart(*file);
    file->flushBuffer();
    QVERIFY(file->checkSizeAgainstHashset());
    QCOMPARE(m_shared->getCount(), 0);

    file->applyCorrectedSize(kRealSize, QStringLiteral("test"));

    QVERIFY(!file->isSizeContradicted());
    QCOMPARE(static_cast<uint64>(file->fileSize()), kRealSize);
    QCOMPARE(file->partCount(), uint16{3});
    QVERIFY(file->fileIdentifier().hasExpectedMD4HashCount());
    QVERIFY(!file->fileIdentifier().md4TailContradictsSize());
    QVERIFY(file->isComplete(0));
    // The new tail is missing
    QVERIFY(!file->isComplete(kWrongSize, kRealSize - 1));
    QVERIFY(!file->gapList().empty());
    QCOMPARE(file->gapList().back().end, kRealSize - 1);
    // Now it may be shared, with the real size
    QCOMPARE(m_shared->getCount(), 1);
}

void tst_SizeContradiction::correctionOutsideRangeIsIgnored()
{
    PartFile* file = makeWrongSizeFile(QStringLiteral("range"));
    QVERIFY(file);
    QVERIFY(file->checkSizeAgainstHashset());
    file->applyCorrectedSize(kWrongSize, QStringLiteral("test"));
    file->applyCorrectedSize(kWrongSize + PARTSIZE, QStringLiteral("test"));
    file->applyCorrectedSize(kWrongSize - 1, QStringLiteral("test"));
    QCOMPARE(static_cast<uint64>(file->fileSize()), kWrongSize);
    QVERIFY(file->isSizeContradicted());
}

void tst_SizeContradiction::failedLookupPausesAndResumeRetries()
{
    PartFile* file = makeWrongSizeFile(QStringLiteral("failed"));
    QVERIFY(file);
    QVERIFY(file->checkSizeAgainstHashset());

    file->failSizeLookup(QStringLiteral("no answer"), QStringLiteral("Kad disabled"));
    QVERIFY(file->isPaused());
    QCOMPARE(file->status(/*ignorePause*/ true), PartFileStatus::Error);

    // Resume is the retry: Error goes, the size stays in doubt
    file->resumeFile();
    QVERIFY(!file->isPaused());
    QVERIFY(file->status(true) != PartFileStatus::Error);
    QVERIFY(file->isSizeContradicted());
}

// ---------------------------------------------------------------------------
// The coordinator
// ---------------------------------------------------------------------------

PartFileSizeLookup::Backend tst_SizeContradiction::stubBackend(Calls& calls,
                                                               PartFileSizeLookup::Net server,
                                                               PartFileSizeLookup::Net kad)
{
    PartFileSizeLookup::Backend b;
    b.serverState = [server] { return server; };
    b.kadState = [kad] { return kad; };
    b.wanted = [](const QByteArray&) { return true; };
    b.apply = [&calls](const QByteArray& hash, uint64 size, const QString& from) {
        calls.applied.append({hash, size});
        calls.appliedFrom = from;
    };
    b.fail = [&calls](const QByteArray& hash, const QString& s, const QString& k) {
        calls.failed.append(hash);
        calls.serverReason = s;
        calls.kadReason = k;
    };
    return b;
}

void tst_SizeContradiction::serverAnswerWinsAndStopsKad()
{
    Calls calls;
    auto b = stubBackend(calls, PartFileSizeLookup::Net::Ready, PartFileSizeLookup::Net::Ready);
    PartFileSizeLookup::Done serverDone;
    b.startServer = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done done) {
        ++calls.serverStarts;
        serverDone = std::move(done);
        return std::function<void()>([&calls] { ++calls.serverCancels; });
    };
    b.startKad = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done) {
        ++calls.kadStarts;
        return std::function<void()>([&calls] { ++calls.kadCancels; });
    };
    PartFileSizeLookup lookup(b);
    lookup.request(hashBytes(1), kWrongSize, kWrongSize + PARTSIZE);
    QTRY_COMPARE(calls.kadStarts, 1);
    QCOMPARE(calls.serverStarts, 1);

    serverDone(kRealSize, {});
    QTRY_COMPARE(calls.applied.size(), 1);
    QCOMPARE(calls.applied.front().second, kRealSize);
    QCOMPARE(calls.appliedFrom, QStringLiteral("server"));
    QCOMPARE(calls.kadCancels, 1);
    QCOMPARE(calls.serverCancels, 0);   // it answered
    QVERIFY(!lookup.isPending(hashBytes(1)));
}

void tst_SizeContradiction::kadAnswersWhenServerFails()
{
    Calls calls;
    auto b = stubBackend(calls, PartFileSizeLookup::Net::Ready, PartFileSizeLookup::Net::Ready);
    b.startServer = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done done) {
        ++calls.serverStarts;
        done(0, QStringLiteral("server X returned no size"));
        return std::function<void()>();
    };
    PartFileSizeLookup::Done kadDone;
    b.startKad = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done done) {
        ++calls.kadStarts;
        kadDone = std::move(done);
        return std::function<void()>([&calls] { ++calls.kadCancels; });
    };
    PartFileSizeLookup lookup(b);
    lookup.request(hashBytes(2), kWrongSize, kWrongSize + PARTSIZE);
    QTRY_COMPARE(calls.kadStarts, 1);
    QTest::qWait(10);
    QVERIFY(calls.failed.isEmpty());   // Kad still running

    kadDone(kRealSize, {});
    QTRY_COMPARE(calls.applied.size(), 1);
    QCOMPARE(calls.appliedFrom, QStringLiteral("Kad"));
}

void tst_SizeContradiction::bothFailReportsBothReasons()
{
    Calls calls;
    auto b = stubBackend(calls, PartFileSizeLookup::Net::Ready, PartFileSizeLookup::Net::Ready);
    b.startServer = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done done) {
        done(0, QStringLiteral("server X returned no size"));
        return std::function<void()>();
    };
    b.startKad = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done done) {
        done(0, QStringLiteral("no Kad source reported a size"));
        return std::function<void()>();
    };
    PartFileSizeLookup lookup(b);
    lookup.request(hashBytes(3), kWrongSize, kWrongSize + PARTSIZE);
    QTRY_COMPARE(calls.failed.size(), 1);
    QVERIFY(calls.applied.isEmpty());
    QCOMPARE(calls.serverReason, QStringLiteral("server X returned no size"));
    QCOMPARE(calls.kadReason, QStringLiteral("no Kad source reported a size"));
}

void tst_SizeContradiction::outOfRangeAnswerIsAFailure()
{
    Calls calls;
    auto b = stubBackend(calls, PartFileSizeLookup::Net::Ready, PartFileSizeLookup::Net::Disabled);
    b.startServer = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done done) {
        done(kWrongSize + PARTSIZE, {});   // one past the range
        return std::function<void()>();
    };
    PartFileSizeLookup lookup(b);
    lookup.request(hashBytes(4), kWrongSize, kWrongSize + PARTSIZE);
    QTRY_COMPARE(calls.failed.size(), 1);
    QVERIFY(calls.applied.isEmpty());
}

void tst_SizeContradiction::disabledNetworkIsNotAsked()
{
    Calls calls;
    auto b = stubBackend(calls, PartFileSizeLookup::Net::Disabled, PartFileSizeLookup::Net::Ready);
    b.startServer = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done) {
        ++calls.serverStarts;
        return std::function<void()>();
    };
    b.startKad = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done done) {
        ++calls.kadStarts;
        done(kRealSize, {});
        return std::function<void()>();
    };
    PartFileSizeLookup lookup(b);
    lookup.request(hashBytes(5), kWrongSize, kWrongSize + PARTSIZE);
    QTRY_COMPARE(calls.applied.size(), 1);
    QCOMPARE(calls.serverStarts, 0);
    QCOMPARE(calls.kadStarts, 1);
}

void tst_SizeContradiction::bothDisabledFailsWithoutAsking()
{
    Calls calls;
    auto b = stubBackend(calls, PartFileSizeLookup::Net::Disabled,
                         PartFileSizeLookup::Net::Disabled);
    b.startServer = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done) {
        ++calls.serverStarts;
        return std::function<void()>();
    };
    b.startKad = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done) {
        ++calls.kadStarts;
        return std::function<void()>();
    };
    PartFileSizeLookup lookup(b);
    lookup.request(hashBytes(6), kWrongSize, kWrongSize + PARTSIZE);
    QTRY_COMPARE(calls.failed.size(), 1);
    QCOMPARE(calls.serverStarts + calls.kadStarts, 0);
    QCOMPARE(calls.serverReason, QStringLiteral("eD2K disabled"));
    QCOMPARE(calls.kadReason, QStringLiteral("Kad disabled"));
}

void tst_SizeContradiction::queuedLookupsRunInTurn()
{
    Calls calls;
    auto b = stubBackend(calls, PartFileSizeLookup::Net::Ready, PartFileSizeLookup::Net::Disabled);
    std::vector<PartFileSizeLookup::Done> pending;
    b.startServer = [&](const QByteArray&, uint64, uint64, PartFileSizeLookup::Done done) {
        ++calls.serverStarts;
        pending.push_back(std::move(done));
        return std::function<void()>();
    };
    PartFileSizeLookup lookup(b);
    lookup.request(hashBytes(7), kWrongSize, kWrongSize + PARTSIZE);
    lookup.request(hashBytes(8), kWrongSize, kWrongSize + PARTSIZE);
    lookup.request(hashBytes(7), kWrongSize, kWrongSize + PARTSIZE);   // repeat: ignored
    QTRY_COMPARE(calls.serverStarts, 1);
    QTest::qWait(10);
    QCOMPARE(calls.serverStarts, 1);   // one at a time

    pending.back()(kRealSize, {});
    QTRY_COMPARE(calls.serverStarts, 2);
    pending.back()(kRealSize, {});
    QTRY_COMPARE(calls.applied.size(), 2);
    QCOMPARE(calls.applied[0].first, hashBytes(7));
    QCOMPARE(calls.applied[1].first, hashBytes(8));
    QTest::qWait(10);
    QCOMPARE(calls.serverStarts, 2);
}

// ---------------------------------------------------------------------------
// Kad size probe
// ---------------------------------------------------------------------------

namespace {

kad::TagList sizeTags(uint64 size)
{
    kad::TagList tags;
    tags.emplace_back(FT_SOURCETYPE, static_cast<uint32>(1));
    tags.emplace_back(FT_SOURCEIP, static_cast<uint32>(0x01020304));
    tags.emplace_back(FT_SOURCEPORT, static_cast<uint32>(4662));
    tags.emplace_back(FT_FILESIZE, size);
    return tags;
}

kad::UInt128 sourceID(uint32 n)
{
    return kad::UInt128(n);
}

} // namespace

void tst_SizeContradiction::kadProbeTakesMostCommonSize()
{
    std::optional<uint64> got;
    {
        kad::Search search;
        search.setSearchType(kad::SearchType::File);
        search.setTargetID(kad::UInt128(reinterpret_cast<const uint8*>(hashBytes(9).constData())));
        search.setSizeProbe(kWrongSize, kWrongSize + PARTSIZE, [&got](uint64 s) { got = s; });

        auto inRange = sizeTags(kRealSize);
        auto other = sizeTags(kRealSize + 1);
        auto bad = sizeTags(kWrongSize);   // the wrong size others published
        search.processResultFile(sourceID(1), inRange);
        search.processResultFile(sourceID(1), inRange);   // same source: one vote
        search.processResultFile(sourceID(2), bad);
        search.processResultFile(sourceID(3), other);
        QVERIFY(!got);   // no two agree yet
    }
    // Ends with the first most-voted size in range
    QVERIFY(got);
    QCOMPARE(*got, kRealSize);
}

void tst_SizeContradiction::kadProbeFinishesOnTwoAgreeingSources()
{
    std::optional<uint64> got;
    int calls = 0;
    {
        kad::Search search;
        search.setSearchType(kad::SearchType::File);
        search.setTargetID(kad::UInt128(reinterpret_cast<const uint8*>(hashBytes(10).constData())));
        search.setSizeProbe(kWrongSize, kWrongSize + PARTSIZE, [&](uint64 s) {
            got = s;
            ++calls;
        });
        auto tags = sizeTags(kRealSize);
        search.processResultFile(sourceID(1), tags);
        search.processResultFile(sourceID(2), tags);
        QVERIFY(got);
        QCOMPARE(*got, kRealSize);
        QVERIFY(search.stopping());
        search.processResultFile(sourceID(3), tags);
    }
    QCOMPARE(calls, 1);   // not again from the destructor
}

void tst_SizeContradiction::kadProbeAddsNoSources()
{
    int sources = 0;
    kad::Kademlia::setKadSourceResultCallback([&sources](const auto&) { ++sources; });
    std::optional<uint64> got;
    {
        kad::Search search;
        search.setSearchType(kad::SearchType::File);
        search.setTargetID(kad::UInt128(reinterpret_cast<const uint8*>(hashBytes(11).constData())));
        search.setSizeProbe(kWrongSize, kWrongSize + PARTSIZE, [&got](uint64 s) { got = s; });
        auto tags = sizeTags(kRealSize);
        search.processResultFile(sourceID(1), tags);
    }
    kad::Kademlia::setKadSourceResultCallback(nullptr);
    QCOMPARE(sources, 0);
    QVERIFY(got);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

PartFile* tst_SizeContradiction::makeWrongSizeFile(const QString& name, bool forgeRoot)
{
    auto file = std::make_unique<PartFile>();
    file->setFileName(name + QStringLiteral(".bin"));
    file->setFileSize(EMFileSize(kWrongSize));
    file->setTmpPath(m_tempDir);

    // The real file's hashset: part 0 real, part 1 any, part 2 = a 1000-byte tail
    Hash part1{};
    part1.fill(0x22);
    part1[0] = static_cast<uint8>(++m_serial);
    const std::vector<Hash> parts{m_goodPartHash, part1, md4(pattern(1000, 7))};
    Hash root = rootOf(parts);
    if (forgeRoot)
        root[0] ^= 0xFF;
    file->setFileHash(root.data());
    if (forgeRoot)
        file->fileIdentifier().getRawMD4HashSet() = parts;
    else if (!file->fileIdentifier().setMD4HashSet(parts))
        return nullptr;
    if (!file->fileIdentifier().hasExpectedMD4HashCount())
        return nullptr;
    if (!file->createPartFile(m_tempDir))
        return nullptr;
    m_file = std::move(file);
    return m_file.get();
}

void tst_SizeContradiction::writeGoodPart(PartFile& file)
{
    for (qsizetype pos = 0; pos < m_goodPart.size(); pos += EMBLOCKSIZE) {
        const qsizetype len = std::min<qsizetype>(EMBLOCKSIZE, m_goodPart.size() - pos);
        const auto start = static_cast<uint64>(pos);
        file.writeToBuffer(static_cast<uint64>(len),
                           reinterpret_cast<const uint8*>(m_goodPart.constData() + pos),
                           start, start + static_cast<uint64>(len) - 1, nullptr, kPeerAddress);
    }
}

QTEST_MAIN(tst_SizeContradiction)
#include "tst_SizeContradiction.moc"

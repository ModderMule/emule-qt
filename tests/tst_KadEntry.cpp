/// @file tst_KadEntry.cpp
/// @brief Tests for KadEntry.h — DHT data entries.

#include "TestHelpers.h"

#include "kademlia/KadEntry.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadUInt128.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QTest>

#include <algorithm>
#include <memory>

using namespace eMule;
using namespace eMule::kad;

class tst_KadEntry : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void addTag_andGetInt();
    void addTag_andGetStr();
    void getTagCount_basic();
    void setFileName_popularity();
    void getCommonFileName();
    void writeTagList_roundTrip();
    void keyEntry_copy();
    void keyEntry_mergeIPs();
    void keyEntry_trustValue();

    // Publisher trust / name-spam defences — MFC Entry.cpp
    void init();
    void cleanup();
    void trust_singlePublisherIsTrusted();
    void trust_sameSubnetSharesPoints();
    void trust_spammerDilutesItself();
    void trust_publisherCapEvictsOldest();
    void cleanUp_publishersExpireAfterADay();
    void addTag_filtersResultOnlyTags();
    void aich_refcountsAndKeepsIndicesStable();
    void merge_fastRefreshDoesNotBumpPopularity();
    void metaTag_matchesOnEqualityNotSubstring();
    void merge_fileNamesCappedDropsLeastPopular();
    void load_fileNamesCapped();
    void addTag_refusesSecondSourceIP();
    void aich_popularitySaturates();
    void aich_expiredPublisherReleasesItsHash();
};

void tst_KadEntry::construct_default()
{
    Entry e;
    QVERIFY(e.m_address.isNull());
    QCOMPARE(e.m_tcpPort, uint16{0});
    QCOMPARE(e.m_udpPort, uint16{0});
    QCOMPARE(e.m_size, uint64{0});
    QVERIFY(!e.m_source);
}

void tst_KadEntry::addTag_andGetInt()
{
    Entry e;
    e.addTag(Tag(QByteArrayLiteral("rating"), uint32{5}));

    uint64 val = 0;
    bool found = e.getIntTagValue(QByteArrayLiteral("rating"), val);
    QVERIFY(found);
    QCOMPARE(val, uint64{5});
}

void tst_KadEntry::addTag_andGetStr()
{
    Entry e;
    e.addTag(Tag(QByteArrayLiteral("comment"), QStringLiteral("Great file")));

    QString val = e.getStrTagValue(QByteArrayLiteral("comment"));
    QCOMPARE(val, QStringLiteral("Great file"));
}

void tst_KadEntry::getTagCount_basic()
{
    Entry e;
    e.addTag(Tag(QByteArrayLiteral("tag1"), uint32{1}));
    e.addTag(Tag(QByteArrayLiteral("tag2"), uint32{2}));

    // Base tag count = 2 stored tags (no virtual tags since no filename/size)
    QCOMPARE(e.getTagCount(), uint32{2});
}

void tst_KadEntry::setFileName_popularity()
{
    Entry e;
    e.setFileName(QStringLiteral("file.mp3"));
    e.setFileName(QStringLiteral("file.mp3")); // same filename, popularity increases
    e.setFileName(QStringLiteral("FILE.MP3")); // case-insensitive match

    // Should have one filename entry with popularity 3
    QCOMPARE(e.getCommonFileName(), QStringLiteral("file.mp3"));
}

void tst_KadEntry::getCommonFileName()
{
    Entry e;
    e.setFileName(QStringLiteral("rare.txt"));
    e.setFileName(QStringLiteral("popular.txt"));
    e.setFileName(QStringLiteral("popular.txt")); // more popular

    // The most popular name should be returned
    QCOMPARE(e.getCommonFileName(), QStringLiteral("popular.txt"));
}

void tst_KadEntry::writeTagList_roundTrip()
{
    Entry e;
    e.m_size = 1024;
    e.setFileName(QStringLiteral("test.dat"));
    e.addTag(Tag(QByteArrayLiteral("rating"), uint32{3}));

    SafeMemFile file;
    e.writeTagList(file);

    // Verify something was written
    QVERIFY(file.length() > 0);
}

void tst_KadEntry::keyEntry_copy()
{
    KeyEntry original;
    original.m_address = Address::fromHostOrder(0x0A000001);
    original.m_size = 5000;
    original.setFileName(QStringLiteral("copied.txt"));

    Entry* copied = original.copy();
    QVERIFY(copied != nullptr);
    QVERIFY(copied->isKeyEntry());
    QCOMPARE(copied->m_address.toUint32(), uint32{0x0A000001});
    QCOMPARE(copied->m_size, uint64{5000});
    QCOMPARE(copied->getCommonFileName(), QStringLiteral("copied.txt"));

    delete copied;
}

void tst_KadEntry::keyEntry_mergeIPs()
{
    KeyEntry entry1;
    entry1.setFileName(QStringLiteral("name1.txt"));

    KeyEntry entry2;
    entry2.setFileName(QStringLiteral("name2.txt"));

    entry1.mergeIPsAndFilenames(&entry2);

    // Both filenames should now be tracked
    // getCommonFileName returns the most popular one
    QVERIFY(!entry1.getCommonFileName().isEmpty());
}

void tst_KadEntry::keyEntry_trustValue()
{
    KeyEntry entry;
    // Without any publishers, trust should be 0
    float trust = entry.getTrustValue();
    QCOMPARE(trust, 0.0f);
}

// ---------------------------------------------------------------------------
// Publisher trust and name-spam defences
//
// A keyword entry's trust value decides whether this node serves it before or
// after untrusted results. Publishers earn points per /24 block, but the points
// for a block are shared across every entry that block published — so one honest
// publisher scores the full 10 while a spammer flooding many entries from one
// network dilutes its own contribution below the 1.0 "trusted" threshold.
//
// Until this was wired up, no publisher IP was ever recorded: the tracking list
// was permanently null, every entry scored 0, and the trusted-first serve
// degenerated into one unranked pass.
// ---------------------------------------------------------------------------

namespace {
/// Publish `entry` as if it arrived from `ip`. A first publish passes nullptr;
/// a replacement passes the entry it supersedes, as Indexed::addKeyword does.
void publishFrom(KeyEntry& entry, uint32 ip, KeyEntry* replaces = nullptr)
{
    entry.m_address = Address::fromHostOrder(ip);
    entry.mergeIPsAndFilenames(replaces);
}

/// What writePublishTrackingDataToFile() stores: AICH hash count and names.
struct TrackingData {
    uint16 aichCount = 0;
    std::vector<std::pair<QString, uint32>> names;
};

TrackingData trackingDataOf(KeyEntry& entry)
{
    SafeMemFile out;
    entry.writePublishTrackingDataToFile(out);
    SafeMemFile in(out.buffer().left(static_cast<qsizetype>(out.length())));
    TrackingData td;
    td.aichCount = in.readUInt16();
    in.seek(qint64{td.aichCount} * 20, SEEK_CUR);
    for (uint32 n = in.readUInt32(); n > 0; --n) {
        QString name = io::readStringUTF8(in);
        td.names.emplace_back(std::move(name), in.readUInt32());
    }
    return td;
}

bool hasName(const TrackingData& td, const QString& name)
{
    return std::ranges::any_of(td.names, [&](const auto& n) { return n.first == name; });
}
} // namespace

void tst_KadEntry::init()
{
    // s_globalPublishIPs is static and shared, so each test starts clean.
    KeyEntry::resetGlobalTrackingMap();
}

void tst_KadEntry::cleanup()
{
    KeyEntry::resetGlobalTrackingMap();
}

void tst_KadEntry::trust_singlePublisherIsTrusted()
{
    KeyEntry entry;
    entry.setFileName(QStringLiteral("ubuntu.iso"));
    publishFrom(entry, 0x0A000001);

    // One publisher, sole occupant of its /24 → the full per-subnet allocation.
    // Callers treat >= 1.0 as trusted, so this must not be the old 0.0.
    QCOMPARE(entry.getTrustValue(), 10.0f);
}

void tst_KadEntry::trust_sameSubnetSharesPoints()
{
    // Two publishers in ONE /24 split that block's allocation between them...
    KeyEntry sameSubnet;
    sameSubnet.setFileName(QStringLiteral("a.iso"));
    publishFrom(sameSubnet, 0x0A000001);
    {
        KeyEntry second;
        second.setFileName(QStringLiteral("a.iso"));
        publishFrom(second, 0x0A000002, &sameSubnet);
        const float sameSubnetTrust = second.getTrustValue();

        // ...whereas two publishers in DIFFERENT /24s each bring a full share.
        KeyEntry crossSubnet;
        crossSubnet.setFileName(QStringLiteral("b.iso"));
        publishFrom(crossSubnet, 0x0A000001);
        KeyEntry third;
        third.setFileName(QStringLiteral("b.iso"));
        publishFrom(third, 0x0B000001, &crossSubnet);

        // This is the whole point of masking to /24: without it, an attacker
        // holding one /24 would look like 256 independent endorsements.
        QVERIFY(third.getTrustValue() > sameSubnetTrust);
    }
}

void tst_KadEntry::trust_spammerDilutesItself()
{
    // One host publishing many different entries: its per-entry contribution
    // falls as 10/N, so at scale each entry drops below the trusted threshold.
    std::vector<std::unique_ptr<KeyEntry>> spam;
    for (int i = 0; i < 50; ++i) {
        auto e = std::make_unique<KeyEntry>();
        e->setFileName(QStringLiteral("spam_%1.iso").arg(i));
        publishFrom(*e, 0x0A000001);
        spam.push_back(std::move(e));
    }
    QVERIFY(spam.front()->getTrustValue() < 1.0f);

    // An honest publisher from an uncontended block is unaffected.
    KeyEntry honest;
    honest.setFileName(QStringLiteral("honest.iso"));
    publishFrom(honest, 0x0C000001);
    QVERIFY(honest.getTrustValue() >= 1.0f);
}

void tst_KadEntry::trust_publisherCapEvictsOldest()
{
    // The list is capped at 100 so calculation and storage stay bounded. The
    // oldest publisher is dropped and its global tracking count released —
    // failing to release would permanently inflate that subnet's divisor and
    // silently drive every entry it ever touched towards untrusted.
    std::unique_ptr<KeyEntry> current = std::make_unique<KeyEntry>();
    current->setFileName(QStringLiteral("popular.iso"));
    publishFrom(*current, 0x0A000001);

    for (uint32 i = 1; i < 105; ++i) {
        auto next = std::make_unique<KeyEntry>();
        next->setFileName(QStringLiteral("popular.iso"));
        // Each publisher gets its own /24 so nothing collapses by subnet.
        publishFrom(*next, 0x0A000001 + (i << 8), current.get());
        current = std::move(next);
    }

    // Capped, not unbounded: 100 publishers each contributing a full share.
    QCOMPARE(current->getTrustValue(), 100 * 10.0f);
}

void tst_KadEntry::cleanUp_publishersExpireAfterADay()
{
    // MFC CKeyEntry::CleanUpTrackedPublishers: KADEMLIAREPUBLISHTIMEK (24 h).
    // A tracked publisher halves what a second one from its /24 contributes.
    const time_t now = time(nullptr);
    {
        KeyEntry kept;
        publishFrom(kept, 0x0B000001);
        kept.cleanUpTrackedPublishers(now + HR2S(23));
        KeyEntry other;
        publishFrom(other, 0x0B000002);
        QCOMPARE(other.getTrustValue(), 5.0f);
    }
    KeyEntry::resetGlobalTrackingMap();
    {
        KeyEntry expired;
        publishFrom(expired, 0x0B000001);
        expired.cleanUpTrackedPublishers(now + HR2S(25));
        KeyEntry other;
        publishFrom(other, 0x0B000002);
        QCOMPARE(other.getTrustValue(), 10.0f);
    }
}

void tst_KadEntry::addTag_filtersResultOnlyTags()
{
    // TAG_PUBLISHINFO and TAG_KADAICHHASHRESULT are generated by *us* when
    // answering a search. Accepting them from a publisher would let it forge its
    // own trust and publisher counts, which we would then re-serve alongside the
    // genuine ones.
    Entry e;
    e.addTag(Tag(QByteArrayLiteral(TAG_PUBLISHINFO), uint32{0xFFFFFFFF}));
    e.addTag(Tag(QByteArrayLiteral(TAG_KADAICHHASHRESULT), QByteArrayLiteral("forged")));
    QCOMPARE(e.getTagCount(), uint32{0});

    // An ordinary tag still goes through.
    e.addTag(Tag(uint8{FT_MEDIA_ARTIST}, QStringLiteral("someone")));
    QCOMPARE(e.getTagCount(), uint32{1});
}

void tst_KadEntry::aich_refcountsAndKeepsIndicesStable()
{
    KeyEntry e;
    const QByteArray hashA(20, '\xAA');
    const QByteArray hashB(20, '\xBB');

    const uint16 idxA = e.addRemoveAICHHash(hashA, true);
    const uint16 idxB = e.addRemoveAICHHash(hashB, true);
    QVERIFY(idxA != idxB);
    QCOMPARE(e.aichHashCount(), uint16{2});

    // Re-adding an existing hash returns the same index rather than growing.
    QCOMPARE(e.addRemoveAICHHash(hashA, true), idxA);
    QCOMPARE(e.aichHashCount(), uint16{2});

    // Removal only decrements popularity — the slot survives, so indices held
    // by publisher records stay valid.
    QCOMPARE(e.addRemoveAICHHash(hashA, false), idxA);
    QCOMPARE(e.aichHashCount(), uint16{2});
    QCOMPARE(e.addRemoveAICHHash(hashB, false), idxB);
    QCOMPARE(e.aichHashCount(), uint16{2});

    // Removing an unknown hash reports the sentinel instead of inventing a slot.
    QCOMPARE(e.addRemoveAICHHash(QByteArray(20, '\xCC'), false), KeyEntry::kNoAICHHash);
}

void tst_KadEntry::merge_fastRefreshDoesNotBumpPopularity()
{
    // getCommonFileName() returns the most popular name, and that is what we
    // serve to searchers. A publisher republishing in a tight loop must not be
    // able to promote its own filename by refreshing repeatedly.
    KeyEntry first;
    first.setFileName(QStringLiteral("real-name.iso"));
    publishFrom(first, 0x0A000001);

    KeyEntry refreshed;
    refreshed.setFileName(QStringLiteral("real-name.iso"));
    publishFrom(refreshed, 0x0A000001, &first);

    // Same publisher, immediately: recorded as a refresh, so no extra publisher
    // and no popularity bump.
    QCOMPARE(refreshed.getCommonFileName(), QStringLiteral("real-name.iso"));
    QCOMPARE(refreshed.getTrustValue(), 10.0f);

    // A *different* publisher naming the same file is a genuine endorsement and
    // does raise that name's popularity above a competing one.
    KeyEntry other;
    other.setFileName(QStringLiteral("real-name.iso"));
    publishFrom(other, 0x0B000001, &refreshed);
    QCOMPARE(other.getCommonFileName(), QStringLiteral("real-name.iso"));
}

void tst_KadEntry::metaTag_matchesOnEqualityNotSubstring()
{
    // A string metatag must match on full-string case-insensitive equality, not
    // substring — the old `contains` served "foobar" for a search of "foo".
    KeyEntry e;
    e.setFileName(QStringLiteral("song.mp3"));
    e.addTag(Tag(static_cast<uint8>(FT_MEDIA_ARTIST), QStringLiteral("foobar")));

    SearchTerm exact;
    exact.type = SearchTerm::Type::MetaTag;
    exact.tag = Tag(static_cast<uint8>(FT_MEDIA_ARTIST), QStringLiteral("FOOBAR")); // case-insensitive
    QVERIFY(e.startSearchTermsMatch(exact));

    SearchTerm substr;
    substr.type = SearchTerm::Type::MetaTag;
    substr.tag = Tag(static_cast<uint8>(FT_MEDIA_ARTIST), QStringLiteral("foo"));
    QVERIFY(!e.startSearchTermsMatch(substr));
}

void tst_KadEntry::merge_fileNamesCappedDropsLeastPopular()
{
    // Distinct names from distinct publishers stay bounded; a new one pushes out
    // the least popular, never a popular one and never itself.
    auto current = std::make_unique<KeyEntry>();
    current->setFileName(QStringLiteral("popular.iso"));
    publishFrom(*current, 0x0A000001);
    uint32 ip = 0x0A000001;
    auto publish = [&](const QString& name) {
        auto next = std::make_unique<KeyEntry>();
        next->setFileName(name);
        ip += 0x100;
        publishFrom(*next, ip, current.get());
        current = std::move(next);
    };
    publish(QStringLiteral("popular.iso"));
    publish(QStringLiteral("popular.iso"));
    for (int i = 0; i < KADEMLIAMAXFILENAMESPERKEY - 1; ++i)
        publish(QStringLiteral("n_%1.iso").arg(i));
    QCOMPARE(trackingDataOf(*current).names.size(), size_t{KADEMLIAMAXFILENAMESPERKEY});

    publish(QStringLiteral("newest.iso"));
    const TrackingData td = trackingDataOf(*current);
    QCOMPARE(td.names.size(), size_t{KADEMLIAMAXFILENAMESPERKEY});
    QVERIFY(hasName(td, QStringLiteral("popular.iso")));
    QVERIFY(hasName(td, QStringLiteral("newest.iso")));
    QVERIFY(!hasName(td, QStringLiteral("n_0.iso")));   // least popular, oldest
    QCOMPARE(current->getCommonFileName(), QStringLiteral("popular.iso"));
}

void tst_KadEntry::load_fileNamesCapped()
{
    // key_index.dat may hold more names than the cap: all are read so the stream
    // stays aligned, only the most popular are kept.
    constexpr uint32 kNames = KADEMLIAMAXFILENAMESPERKEY + 8;
    constexpr uint32 kSentinel = 0xC0FFEE42;
    SafeMemFile out;
    out.writeUInt32(kNames);
    for (uint32 i = 1; i <= kNames; ++i) {
        io::writeStringUTF8(out, QStringLiteral("name_%1").arg(i));
        out.writeUInt32(i);   // popularity
    }
    out.writeUInt32(0);   // publishers
    out.writeUInt32(kSentinel);

    SafeMemFile in(out.buffer().left(static_cast<qsizetype>(out.length())));
    KeyEntry e;
    e.readPublishTrackingDataFromFile(in, false);
    QCOMPARE(in.readUInt32(), kSentinel);

    const TrackingData td = trackingDataOf(e);
    QCOMPARE(td.names.size(), size_t{KADEMLIAMAXFILENAMESPERKEY});
    const auto least = std::ranges::min(td.names, {}, &std::pair<QString, uint32>::second);
    QCOMPARE(least.second, kNames - KADEMLIAMAXFILENAMESPERKEY + 1);
    QCOMPARE(e.getCommonFileName(), QStringLiteral("name_%1").arg(kNames));
}

void tst_KadEntry::addTag_refusesSecondSourceIP()
{
    // Our FT_SOURCEIP comes first; a second one (old src_index.dat with a
    // publisher-injected tag) must not be stored, nor reported as kept.
    Entry e;
    QVERIFY(e.addTag(Tag(uint8{FT_SOURCEIP}, uint32{0x0A000001})));
    QVERIFY(!e.addTag(Tag(uint8{FT_SOURCEIP}, uint32{0x0B000001})));
    QCOMPARE(e.getTagCount(), uint32{1});
    QCOMPARE(e.getIntTagValue(QByteArrayLiteral(TAG_SOURCEIP)), uint64{0x0A000001});
}

void tst_KadEntry::aich_popularitySaturates()
{
    // uint8 popularity: 256 adds wrapped to 0, which made the hash look unused.
    KeyEntry e;
    const QByteArray hash(20, '\xAA');
    for (int i = 0; i < 256; ++i)
        e.addRemoveAICHHash(hash, true);
    QCOMPARE(trackingDataOf(e).aichCount, uint16{1});
}

void tst_KadEntry::aich_expiredPublisherReleasesItsHash()
{
    // An expired publisher drops its AICH ref, as the 100-publisher cap does.
    const time_t now = time(nullptr);
    KeyEntry e;
    e.addRemoveAICHHash(QByteArray(20, '\xAA'), true);   // what the publish reported
    publishFrom(e, 0x0A000001);
    QCOMPARE(trackingDataOf(e).aichCount, uint16{1});

    e.cleanUpTrackedPublishers(now + HR2S(23));
    QCOMPARE(trackingDataOf(e).aichCount, uint16{1});
    e.cleanUpTrackedPublishers(now + HR2S(25));
    QCOMPARE(trackingDataOf(e).aichCount, uint16{0});
}

QTEST_GUILESS_MAIN(tst_KadEntry)
#include "tst_KadEntry.moc"

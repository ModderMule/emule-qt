/// @file tst_Collection.cpp
/// @brief Tests for Collection container — multi-file management, write/read
///        round-trips (binary + text), deep copy, author key helpers, and
///        IPC serialization contract.

#include "TestHelpers.h"
#include "files/Collection.h"
#include "files/CollectionFile.h"
#include "files/CollectionKeys.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"
#include "files/KnownFile.h"
#include "files/PublishKeywordList.h"
#include "files/ShareableFile.h"
#include "kademlia/KadMiscUtils.h"
#include "utils/OtherFunctions.h"

#include <QCborArray>
#include <QCborMap>
#include <QCryptographicHash>
#include <QFile>
#include <QTest>

#include <openssl/evp.h>

#include <algorithm>
#include <cstring>
#include <memory>

using namespace eMule;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// Helper: create a ShareableFile with a 1-byte-repeated hash
// ---------------------------------------------------------------------------

static ShareableFile makeTestFile(uint8 hashByte, const QString& name, uint64 size)
{
    ShareableFile f;
    uint8 hash[16];
    std::memset(hash, hashByte, 16);
    f.setFileHash(hash);
    f.setFileName(name, true);
    f.setFileSize(size);
    return f;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_Collection : public QObject {
    Q_OBJECT

private slots:
    void addFiles_threePlusFiles();
    void writeAndRead_binaryRoundTrip();
    void writeAndRead_textRoundTrip();
    void copyFrom_deepCopy();
    void authorKeyHelpers();
    void authorKeyIsPublishedAsKadKeyword();
    void ipcSerialization_matchesHandlerFormat();
    void truncatedBinary_neverThrows();
    void signature_isSha1AndStillReadsSha256();
    void unsignedCollection_showsNoAuthor();
    void textCollection_writesRealLinks();
};

// ---------------------------------------------------------------------------
// addFiles_threePlusFiles
// ---------------------------------------------------------------------------

void tst_Collection::addFiles_threePlusFiles()
{
    auto f1 = makeTestFile(0xAA, QStringLiteral("movie.mkv"),    1'500'000'000);
    auto f2 = makeTestFile(0xBB, QStringLiteral("album.zip"),      350'000'000);
    auto f3 = makeTestFile(0xCC, QStringLiteral("document.pdf"),     2'500'000);

    Collection coll;
    QVERIFY(coll.addFile(&f1));
    QVERIFY(coll.addFile(&f2));
    QVERIFY(coll.addFile(&f3));
    QCOMPARE(coll.fileCount(), 3);

    // Verify each file is retrievable by hash key
    for (const auto& [key, cf] : coll.files()) {
        QVERIFY(!cf->fileName().isEmpty());
        QVERIFY(cf->fileSize() > 0);
    }

    // Adding a duplicate returns existing entry, count unchanged
    auto* dup = coll.addFile(&f1);
    QVERIFY(dup != nullptr);
    QCOMPARE(coll.fileCount(), 3);
}

// ---------------------------------------------------------------------------
// writeAndRead_binaryRoundTrip
// ---------------------------------------------------------------------------

void tst_Collection::writeAndRead_binaryRoundTrip()
{
    auto f1 = makeTestFile(0x11, QStringLiteral("file_one.bin"),   100'000);
    auto f2 = makeTestFile(0x22, QStringLiteral("file_two.dat"), 2'000'000);
    auto f3 = makeTestFile(0x33, QStringLiteral("file_three.txt"),  50'000);

    Collection original;
    original.m_name = QStringLiteral("Test Collection");
    original.m_textFormat = false;
    original.addFile(&f1);
    original.addFile(&f2);
    original.addFile(&f3);

    TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("test.emulecollection"));
    QVERIFY(original.writeToFile(path));

    // Read back
    Collection loaded;
    QVERIFY(loaded.initFromFile(path, QStringLiteral("test.emulecollection")));

    QCOMPARE(loaded.m_name, QStringLiteral("Test Collection"));
    QCOMPARE(loaded.fileCount(), 3);
    QVERIFY(!loaded.m_textFormat);

    // Verify each original file is present in loaded collection
    for (const auto& [key, origCf] : original.files()) {
        auto it = loaded.files().find(key);
        QVERIFY2(it != loaded.files().end(), "File not found in loaded collection");
        QCOMPARE(it->second->fileName(), origCf->fileName());
        QCOMPARE(it->second->fileSize(), origCf->fileSize());
        QVERIFY(md4equ(it->second->fileHash(), origCf->fileHash()));
    }
}

// ---------------------------------------------------------------------------
// writeAndRead_textRoundTrip
// ---------------------------------------------------------------------------

void tst_Collection::writeAndRead_textRoundTrip()
{
    auto f1 = makeTestFile(0x44, QStringLiteral("alpha.mp3"),  5'000'000);
    auto f2 = makeTestFile(0x55, QStringLiteral("beta.flac"), 40'000'000);
    auto f3 = makeTestFile(0x66, QStringLiteral("gamma.ogg"),  3'000'000);

    Collection original;
    original.m_name = QStringLiteral("Music Collection");
    original.m_textFormat = true;
    original.addFile(&f1);
    original.addFile(&f2);
    original.addFile(&f3);

    TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("music.emulecollection"));
    QVERIFY(original.writeToFile(path));

    // Read back — text format derives name from filename
    Collection loaded;
    QVERIFY(loaded.initFromFile(path, QStringLiteral("music.emulecollection")));

    QCOMPARE(loaded.m_name, QStringLiteral("music"));  // stripped .emulecollection
    QCOMPARE(loaded.fileCount(), 3);
    QVERIFY(loaded.m_textFormat);

    // Verify all files are present with correct data
    for (const auto& [key, origCf] : original.files()) {
        auto it = loaded.files().find(key);
        QVERIFY2(it != loaded.files().end(), "File not found in loaded text collection");
        QCOMPARE(it->second->fileName(), origCf->fileName());
        QCOMPARE(it->second->fileSize(), origCf->fileSize());
    }
}

// ---------------------------------------------------------------------------
// copyFrom_deepCopy
// ---------------------------------------------------------------------------

void tst_Collection::copyFrom_deepCopy()
{
    auto f1 = makeTestFile(0xA1, QStringLiteral("one.bin"),   1000);
    auto f2 = makeTestFile(0xB2, QStringLiteral("two.bin"),   2000);
    auto f3 = makeTestFile(0xC3, QStringLiteral("three.bin"), 3000);

    Collection original;
    original.m_name = QStringLiteral("Original");
    original.m_authorName = QStringLiteral("TestAuthor");
    original.m_authorKey = QByteArray("\xDE\xAD\xBE\xEF", 4);
    original.addFile(&f1);
    original.addFile(&f2);
    original.addFile(&f3);

    Collection copy;
    copy.copyFrom(original);

    // All fields match
    QCOMPARE(copy.m_name, original.m_name);
    QCOMPARE(copy.m_authorName, original.m_authorName);
    QCOMPARE(copy.m_authorKey, original.m_authorKey);
    QCOMPARE(copy.m_textFormat, original.m_textFormat);
    QCOMPARE(copy.fileCount(), original.fileCount());

    // Verify deep copy — modifying copy doesn't affect original
    copy.m_name = QStringLiteral("Modified Copy");
    QCOMPARE(original.m_name, QStringLiteral("Original"));

    // Verify file contents match
    for (const auto& [key, origCf] : original.files()) {
        auto it = copy.files().find(key);
        QVERIFY(it != copy.files().end());
        QCOMPARE(it->second->fileName(), origCf->fileName());
        QCOMPARE(it->second->fileSize(), origCf->fileSize());
    }
}

// ---------------------------------------------------------------------------
// authorKeyHelpers
// ---------------------------------------------------------------------------

void tst_Collection::authorKeyHelpers()
{
    Collection coll;

    // Empty key → empty strings
    QVERIFY(coll.authorKeyHashString().isEmpty());
    QVERIFY(coll.authorKeyString().isEmpty());

    // Set known bytes
    coll.m_authorKey = QByteArray("\xDE\xAD\xBE\xEF", 4);

    // authorKeyString = raw hex
    QCOMPARE(coll.authorKeyString(), QStringLiteral("deadbeef"));

    // authorKeyHashString = MD5 of the key bytes, uppercase hex
    // MD5("\xDE\xAD\xBE\xEF") = 2F249230A8E7C2BF6005CCD2679259EC
    QCOMPARE(coll.authorKeyHashString(), QStringLiteral("2F249230A8E7C2BF6005CCD2679259EC"));
}

// ---------------------------------------------------------------------------
// authorKeyIsPublishedAsKadKeyword
//
// "Search Author's Collections" is an ordinary Kad keyword search on the author's
// public key, so a shared collection must be indexed under that key. Guards both
// halves of that chain: KnownFile rebuilding its keyword list when the collection is
// attached, and PublishKeywordList publishing the list rather than re-tokenizing the
// file name. Mirrors srchybrid/SharedFileList.cpp:703-721.
// ---------------------------------------------------------------------------

void tst_Collection::authorKeyIsPublishedAsKadKeyword()
{
    KnownFile kf;
    uint8 hash[16];
    std::memset(hash, 0xC0, 16);
    kf.setFileHash(hash);
    kf.setFileName(QStringLiteral("mixtape.emulecollection"), true);

    // Without a collection the keywords come from the file name alone.
    QVERIFY(!kf.kadKeywords().empty());
    const QString keyHex = QStringLiteral("deadbeef");
    QVERIFY(!std::ranges::contains(kf.kadKeywords(), keyHex));

    auto coll = std::make_unique<Collection>();
    coll->m_name = QStringLiteral("Mixtape");
    coll->m_authorKey = QByteArray("\xDE\xAD\xBE\xEF", 4);
    kf.setCollection(std::move(coll));

    // setCollection re-runs setFileName, which prepends the author key.
    QVERIFY(std::ranges::contains(kf.kadKeywords(), keyHex));

    // And the publisher must use that list, not the file name.
    PublishKeywordList keywords;
    keywords.addKeywords(&kf);
    keywords.resetNextKeyword();

    bool published = false;
    while (const PublishKeyword* kw = keywords.getNextKeyword()) {
        if (kad::kadTagStrToLower(kw->keyword()) == keyHex) {
            published = true;
            break;
        }
    }
    QVERIFY2(published, "author key was not queued for Kad publishing");
}

// ---------------------------------------------------------------------------
// ipcSerialization_matchesHandlerFormat
// ---------------------------------------------------------------------------

void tst_Collection::ipcSerialization_matchesHandlerFormat()
{
    // Create a collection with 3 files and author info — same as what
    // handleGetCollectionInfo serializes to QCborMap
    auto f1 = makeTestFile(0xD1, QStringLiteral("pic.jpg"),     500'000);
    auto f2 = makeTestFile(0xD2, QStringLiteral("vid.mp4"), 100'000'000);
    auto f3 = makeTestFile(0xD3, QStringLiteral("doc.pdf"),   1'200'000);

    Collection coll;
    coll.m_name = QStringLiteral("IPC Test Collection");
    coll.m_authorName = QStringLiteral("Alice");
    coll.m_authorKey = QByteArray(32, '\xAB');  // 32 fake key bytes
    coll.addFile(&f1);
    coll.addFile(&f2);
    coll.addFile(&f3);

    // Build the QCborMap exactly as handleGetCollectionInfo does
    QCborMap result;
    result.insert(QStringLiteral("name"), coll.m_name);
    result.insert(QStringLiteral("authorName"), coll.m_authorName);
    result.insert(QStringLiteral("authorKeyHash"), coll.authorKeyHashString());
    result.insert(QStringLiteral("authorKeyHex"), coll.authorKeyString());
    result.insert(QStringLiteral("textFormat"), coll.m_textFormat);

    QCborArray filesArr;
    for (const auto& [key, cf] : coll.files()) {
        QCborMap fm;
        fm.insert(QStringLiteral("hash"), md4str(cf->fileHash()));
        fm.insert(QStringLiteral("fileName"), cf->fileName());
        fm.insert(QStringLiteral("fileSize"), static_cast<qint64>(cf->fileSize()));
        filesArr.append(fm);
    }
    result.insert(QStringLiteral("files"), filesArr);

    // Verify the CBOR map fields
    QCOMPARE(result.value(QStringLiteral("name")).toString(),
             QStringLiteral("IPC Test Collection"));
    QCOMPARE(result.value(QStringLiteral("authorName")).toString(),
             QStringLiteral("Alice"));
    QVERIFY(!result.value(QStringLiteral("authorKeyHash")).toString().isEmpty());
    QVERIFY(!result.value(QStringLiteral("authorKeyHex")).toString().isEmpty());
    QCOMPARE(result.value(QStringLiteral("textFormat")).toBool(), false);

    // Verify files array
    const QCborArray files = result.value(QStringLiteral("files")).toArray();
    QCOMPARE(files.size(), 3);

    // Collect all file names from the CBOR array
    QStringList names;
    for (const auto& v : files) {
        const QCborMap fm = v.toMap();
        names.append(fm.value(QStringLiteral("fileName")).toString());
        // Verify hash is 32-char hex string
        QCOMPARE(fm.value(QStringLiteral("hash")).toString().size(), 32);
        // Verify size is positive
        QVERIFY(fm.value(QStringLiteral("fileSize")).toInteger() > 0);
    }

    // All 3 test files should be present
    QVERIFY(names.contains(QStringLiteral("pic.jpg")));
    QVERIFY(names.contains(QStringLiteral("vid.mp4")));
    QVERIFY(names.contains(QStringLiteral("doc.pdf")));
}

// ---------------------------------------------------------------------------
// C51: a cut-off collection must not throw (it is parsed inside a slot)
// ---------------------------------------------------------------------------

static QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static bool writeAll(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

void tst_Collection::truncatedBinary_neverThrows()
{
    auto f1 = makeTestFile(0x11, QStringLiteral("file_one.bin"),   100'000);
    auto f2 = makeTestFile(0x22, QStringLiteral("file_two.dat"), 2'000'000);
    auto f3 = makeTestFile(0x33, QStringLiteral("file_three.txt"),  50'000);

    Collection original;
    original.m_name = QStringLiteral("Cut");
    original.addFile(&f1);
    original.addFile(&f2);
    original.addFile(&f3);

    TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("cut.emulecollection"));
    QVERIFY(original.writeToFile(path));
    const QByteArray whole = readAll(path);
    QVERIFY(whole.size() > 40);

    int partial = 0;
    for (qsizetype len = 8; len < whole.size(); ++len) {
        QVERIFY(writeAll(path, whole.left(len)));
        Collection loaded;
        loaded.m_authorName = QStringLiteral("stale");
        bool ok = false;
        try {
            ok = loaded.initFromFile(path, QStringLiteral("cut.emulecollection"));
        } catch (...) {
            QFAIL(qPrintable(QStringLiteral("threw at length %1").arg(len)));
        }
        QVERIFY(loaded.m_authorName.isEmpty());
        QVERIFY(loaded.fileCount() < 3);
        QCOMPARE(ok, loaded.fileCount() > 0);
        if (ok)
            ++partial;
    }
    QVERIFY(partial > 0);   // entries before the cut are kept, as MFC
}

// ---------------------------------------------------------------------------
// C52: MFC signs RSA-SHA1; builds up to 0.6.2 signed SHA-256
// ---------------------------------------------------------------------------

static QByteArray signWith(const EVP_MD* md, EVP_PKEY* key, const QByteArray& payload)
{
    QByteArray sig;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    size_t len = 0;
    if (ctx && EVP_DigestSignInit(ctx, nullptr, md, nullptr, key) > 0
        && EVP_DigestSignUpdate(ctx, payload.constData(), static_cast<size_t>(payload.size())) > 0
        && EVP_DigestSignFinal(ctx, nullptr, &len) > 0) {
        sig.resize(static_cast<qsizetype>(len));
        if (EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char*>(sig.data()), &len) > 0)
            sig.resize(static_cast<qsizetype>(len));
        else
            sig.clear();
    }
    EVP_MD_CTX_free(ctx);
    return sig;
}

void tst_Collection::signature_isSha1AndStillReadsSha256()
{
    TempDir tmp;
    CollectionKeys keys(tmp.path());
    QVERIFY(keys.initialize());
    QVERIFY(keys.signKey() != nullptr);

    auto f1 = makeTestFile(0x71, QStringLiteral("signed_one.bin"), 1000);
    auto f2 = makeTestFile(0x72, QStringLiteral("signed_two.bin"), 2000);

    Collection original;
    original.m_name = QStringLiteral("Signed");
    original.m_authorName = QStringLiteral("Author");
    original.m_authorKey = keys.publicKeyDer();
    original.addFile(&f1);
    original.addFile(&f2);

    const QString path = tmp.filePath(QStringLiteral("signed.emulecollection"));
    QVERIFY(original.writeToFile(path, keys.signKey()));
    const QByteArray whole = readAll(path);

    Collection loaded;
    QVERIFY(loaded.initFromFile(path, QStringLiteral("signed.emulecollection")));
    QCOMPARE(loaded.m_authorName, QStringLiteral("Author"));
    QCOMPARE(loaded.m_authorKey, keys.publicKeyDer());

    // The signature on disk is SHA-1 over everything before it.
    const qsizetype sigLen = 128;   // 1024-bit key
    const QByteArray payload = whole.left(whole.size() - sigLen);
    QCOMPARE(whole.right(sigLen), signWith(EVP_sha1(), keys.signKey(), payload));

    // One changed byte: files stay, the author goes.
    QByteArray tampered = whole;
    tampered[payload.size() - 1] = static_cast<char>(tampered[payload.size() - 1] ^ 0x01);
    QVERIFY(writeAll(path, tampered));
    Collection bad;
    bad.initFromFile(path, QStringLiteral("signed.emulecollection"));
    QVERIFY(bad.m_authorName.isEmpty());
    QVERIFY(bad.m_authorKey.isEmpty());

    // A collection an older build signed keeps its author.
    QVERIFY(writeAll(path, payload + signWith(EVP_sha256(), keys.signKey(), payload)));
    Collection legacy;
    QVERIFY(legacy.initFromFile(path, QStringLiteral("signed.emulecollection")));
    QCOMPARE(legacy.m_authorName, QStringLiteral("Author"));
}

// ---------------------------------------------------------------------------
// C61: an author without a key is only a claim (MFC Collection.cpp:188-189)
// ---------------------------------------------------------------------------

void tst_Collection::unsignedCollection_showsNoAuthor()
{
    auto f1 = makeTestFile(0x81, QStringLiteral("claimed.bin"), 1000);
    CollectionFile entry(&f1);

    SafeMemFile mem;
    mem.writeUInt32(kCollectionFileVersion1);
    mem.writeUInt32(2);
    Tag(FT_FILENAME, QStringLiteral("Claimed")).writeNewEd2kTag(mem, UTF8Mode::Raw);
    Tag(FT_COLLECTIONAUTHOR, QStringLiteral("Somebody Famous")).writeNewEd2kTag(mem, UTF8Mode::Raw);
    mem.writeUInt32(1);
    QVERIFY(entry.writeCollectionInfo(mem));

    TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("claimed.emulecollection"));
    QVERIFY(writeAll(path, mem.buffer()));

    Collection loaded;
    QVERIFY(loaded.initFromFile(path, QStringLiteral("claimed.emulecollection")));
    QCOMPARE(loaded.m_name, QStringLiteral("Claimed"));
    QCOMPARE(loaded.fileCount(), 1);
    QVERIFY(loaded.m_authorName.isEmpty());
}

// ---------------------------------------------------------------------------
// C63: text lines are real links (encoded name, AICH), as MFC's GetED2kLink()
// ---------------------------------------------------------------------------

void tst_Collection::textCollection_writesRealLinks()
{
    auto f1 = makeTestFile(0x91, QStringLiteral("a b|c.mp3"), 5'000'000);
    AICHHash aich;
    std::memset(aich.getRawHash(), 0x42, kAICHHashSize);
    f1.fileIdentifier().setAICHHash(aich);

    Collection original;
    original.m_textFormat = true;
    original.addFile(&f1);

    TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("links.emulecollection"));
    QVERIFY(original.writeToFile(path));

    const QString line = QString::fromUtf8(readAll(path)).trimmed();
    QVERIFY2(line.contains(QStringLiteral("|h=")), qPrintable(line));
    QVERIFY2(!line.contains(u' '), qPrintable(line));
    QCOMPARE(line.count(u'|'), 6);   // the name's own bar is encoded

    Collection loaded;
    QVERIFY(loaded.initFromFile(path, QStringLiteral("links.emulecollection")));
    QCOMPARE(loaded.fileCount(), 1);
    const auto& cf = loaded.files().begin()->second;
    QVERIFY(cf->fileIdentifier().hasAICHHash());
    QCOMPARE(cf->fileIdentifier().getAICHHash(), aich);
}

QTEST_MAIN(tst_Collection)
#include "tst_Collection.moc"

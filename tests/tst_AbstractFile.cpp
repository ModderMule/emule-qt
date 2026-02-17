/// @file tst_AbstractFile.cpp
/// @brief Tests for files/AbstractFile — tags, filename, filetype, hash, ED2K link.

#include "TestHelpers.h"
#include "files/AbstractFile.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QTest>
#include <cstring>

using namespace eMule;

// Concrete subclass for testing (AbstractFile is abstract)
class TestFile : public AbstractFile {
public:
    using AbstractFile::AbstractFile;
    void updateFileRatingCommentAvail(bool /*forceUpdate*/ = false) override
    {
        ++updateCallCount;
    }
    int updateCallCount = 0;
};

class tst_AbstractFile : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void construct_copy();
    void setFileName_basic();
    void setFileName_invalidChars();
    void setFileName_autoFileType();
    void setFileName_removeControlChars();
    void setFileType_basic();
    void fileSize_basic();
    void isLargeFile();
    void hasNullHash_initial();
    void hasNullHash_afterSet();
    void setFileHash();
    void getED2kLink_basic();
    void getED2kLink_html();
    void getED2kLink_aichHash();
    void intTag_setAndGet();
    void intTag_overwrite();
    void int64Tag_setAndGet();
    void strTag_setAndGet();
    void strTag_overwrite();
    void getTag_byIdAndType();
    void getTag_byName();
    void addTagUnique_replaces();
    void addTagUnique_appends();
    void deleteTag_basic();
    void clearTags_basic();
    void copyTags_basic();
    void tagByName_int();
    void tagByName_int64();
    void tagByName_str();
    void rating_and_comment();
    void kadCommentSearchRunning();
    void getFileTypeByName_basic();
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void tst_AbstractFile::construct_default()
{
    TestFile file;
    QCOMPARE(file.fileSize(), EMFileSize{0});
    QVERIFY(file.fileName().isEmpty());
    QVERIFY(file.fileType().isEmpty());
    QVERIFY(file.hasNullHash());
    QVERIFY(file.tags().empty());
    QVERIFY(!file.hasComment());
    QVERIFY(!file.hasRating());
    QVERIFY(!file.isPartFile());
    QVERIFY(!file.isLargeFile());
}

void tst_AbstractFile::construct_copy()
{
    TestFile src;
    src.setFileName(QStringLiteral("test.mp3"));
    src.setFileSize(1000);
    src.setUserRating(3);

    uint8 hash[16];
    std::memset(hash, 0xAB, 16);
    src.setFileHash(hash);

    src.setIntTagValue(FT_SOURCES, 42);

    TestFile copy(src);
    QCOMPARE(copy.fileName(), QStringLiteral("test.mp3"));
    QCOMPARE(copy.fileSize(), EMFileSize{1000});
    QCOMPARE(copy.userRating(), uint32{3});
    QVERIFY(!copy.hasNullHash());
    QVERIFY(md4equ(copy.fileHash(), hash));
    QCOMPARE(copy.getIntTagValue(FT_SOURCES), uint32{42});
    QCOMPARE(copy.fileType(), QStringLiteral(ED2KFTSTR_AUDIO));
}

void tst_AbstractFile::setFileName_basic()
{
    TestFile file;
    file.setFileName(QStringLiteral("my_file.txt"), false, false);
    QCOMPARE(file.fileName(), QStringLiteral("my_file.txt"));
    QVERIFY(file.fileType().isEmpty()); // autoSetFileType=false
}

void tst_AbstractFile::setFileName_invalidChars()
{
    TestFile file;
    file.setFileName(QStringLiteral("file<name>.txt"), true, false);
    QCOMPARE(file.fileName(), QStringLiteral("file-name-.txt"));
}

void tst_AbstractFile::setFileName_autoFileType()
{
    TestFile file;
    file.setFileName(QStringLiteral("video.mkv"));
    QCOMPARE(file.fileType(), QStringLiteral(ED2KFTSTR_VIDEO));

    file.setFileName(QStringLiteral("song.mp3"));
    QCOMPARE(file.fileType(), QStringLiteral(ED2KFTSTR_AUDIO));

    file.setFileName(QStringLiteral("doc.pdf"));
    QCOMPARE(file.fileType(), QStringLiteral(ED2KFTSTR_DOCUMENT));

    file.setFileName(QStringLiteral("archive.rar"));
    QCOMPARE(file.fileType(), QStringLiteral(ED2KFTSTR_ARCHIVE));

    file.setFileName(QStringLiteral("disc.iso"));
    QCOMPARE(file.fileType(), QStringLiteral(ED2KFTSTR_CDIMAGE));

    file.setFileName(QStringLiteral("photo.jpg"));
    QCOMPARE(file.fileType(), QStringLiteral(ED2KFTSTR_IMAGE));

    file.setFileName(QStringLiteral("app.exe"));
    QCOMPARE(file.fileType(), QStringLiteral(ED2KFTSTR_PROGRAM));
}

void tst_AbstractFile::setFileName_removeControlChars()
{
    TestFile file;
    QString name = QStringLiteral("file") + QChar(0x01) + QStringLiteral("name.txt");
    file.setFileName(name, false, false, true);
    QCOMPARE(file.fileName(), QStringLiteral("filename.txt"));
}

void tst_AbstractFile::setFileType_basic()
{
    TestFile file;
    file.setFileType(QStringLiteral("Audio"));
    QCOMPARE(file.fileType(), QStringLiteral("Audio"));
}

void tst_AbstractFile::fileSize_basic()
{
    TestFile file;
    file.setFileSize(12345678);
    QCOMPARE(file.fileSize(), EMFileSize{12345678});
}

void tst_AbstractFile::isLargeFile()
{
    TestFile file;
    file.setFileSize(OLD_MAX_EMULE_FILE_SIZE);
    QVERIFY(!file.isLargeFile());

    file.setFileSize(OLD_MAX_EMULE_FILE_SIZE + 1);
    QVERIFY(file.isLargeFile());
}

void tst_AbstractFile::hasNullHash_initial()
{
    TestFile file;
    QVERIFY(file.hasNullHash());
}

void tst_AbstractFile::hasNullHash_afterSet()
{
    TestFile file;
    uint8 hash[16];
    std::memset(hash, 0xAB, 16);
    file.setFileHash(hash);
    QVERIFY(!file.hasNullHash());
}

void tst_AbstractFile::setFileHash()
{
    TestFile file;
    uint8 hash[16];
    std::memset(hash, 0x42, 16);
    file.setFileHash(hash);
    QVERIFY(md4equ(file.fileHash(), hash));
}

void tst_AbstractFile::getED2kLink_basic()
{
    TestFile file;
    file.setFileName(QStringLiteral("test file.txt"), false, false);
    file.setFileSize(12345);
    uint8 hash[16];
    std::memset(hash, 0xAB, 16);
    file.setFileHash(hash);

    QString link = file.getED2kLink();
    QVERIFY(link.startsWith(QStringLiteral("ed2k://|file|")));
    QVERIFY(link.contains(QStringLiteral("|12345|")));
    QVERIFY(link.contains(encodeBase16({hash, 16})));
    QVERIFY(link.endsWith(QChar(u'/')));
}

void tst_AbstractFile::getED2kLink_html()
{
    TestFile file;
    file.setFileName(QStringLiteral("test.txt"), false, false);
    file.setFileSize(100);
    uint8 hash[16] = {};
    file.setFileHash(hash);

    QString link = file.getED2kLink(false, true);
    QVERIFY(link.startsWith(QStringLiteral("<a href=\"")));
    QVERIFY(link.endsWith(QStringLiteral("</a>")));
}

void tst_AbstractFile::getED2kLink_aichHash()
{
    TestFile file;
    file.setFileName(QStringLiteral("test.txt"), false, false);
    file.setFileSize(100);
    uint8 hash[16] = {};
    file.setFileHash(hash);

    // Set an AICH hash
    AICHHash aichHash;
    file.fileIdentifier().setAICHHash(aichHash);

    QString link = file.getED2kLink();
    QVERIFY(link.contains(QStringLiteral("h=")));
}

void tst_AbstractFile::intTag_setAndGet()
{
    TestFile file;
    file.setIntTagValue(FT_SOURCES, 99);
    QCOMPARE(file.getIntTagValue(FT_SOURCES), uint32{99});
}

void tst_AbstractFile::intTag_overwrite()
{
    TestFile file;
    file.setIntTagValue(FT_SOURCES, 10);
    file.setIntTagValue(FT_SOURCES, 20);
    QCOMPARE(file.getIntTagValue(FT_SOURCES), uint32{20});
    QCOMPARE(file.tags().size(), std::size_t{1}); // only one tag
}

void tst_AbstractFile::int64Tag_setAndGet()
{
    TestFile file;
    file.setInt64TagValue(FT_FILESIZE, uint64{0x100000000ULL});
    QCOMPARE(file.getInt64TagValue(FT_FILESIZE), uint64{0x100000000ULL});
}

void tst_AbstractFile::strTag_setAndGet()
{
    TestFile file;
    file.setStrTagValue(FT_FILENAME, QStringLiteral("test.txt"));
    QCOMPARE(file.getStrTagValue(FT_FILENAME), QStringLiteral("test.txt"));
}

void tst_AbstractFile::strTag_overwrite()
{
    TestFile file;
    file.setStrTagValue(FT_FILENAME, QStringLiteral("old.txt"));
    file.setStrTagValue(FT_FILENAME, QStringLiteral("new.txt"));
    QCOMPARE(file.getStrTagValue(FT_FILENAME), QStringLiteral("new.txt"));
    QCOMPARE(file.tags().size(), std::size_t{1});
}

void tst_AbstractFile::getTag_byIdAndType()
{
    TestFile file;
    file.setIntTagValue(FT_SOURCES, 5);

    const Tag* tag = file.getTag(FT_SOURCES, TAGTYPE_UINT32);
    QVERIFY(tag != nullptr);
    QCOMPARE(tag->intValue(), uint32{5});

    // Wrong type should return null
    QVERIFY(file.getTag(FT_SOURCES, TAGTYPE_STRING) == nullptr);
}

void tst_AbstractFile::getTag_byName()
{
    TestFile file;
    QByteArray tagName("customTag");
    file.addTagUnique(Tag(tagName, uint32{42}));

    const Tag* tag = file.getTag(tagName);
    QVERIFY(tag != nullptr);
    QCOMPARE(tag->intValue(), uint32{42});
}

void tst_AbstractFile::addTagUnique_replaces()
{
    TestFile file;
    file.addTagUnique(Tag(FT_SOURCES, uint32{10}));
    file.addTagUnique(Tag(FT_SOURCES, uint32{20}));
    QCOMPARE(file.tags().size(), std::size_t{1});
    QCOMPARE(file.getIntTagValue(FT_SOURCES), uint32{20});
}

void tst_AbstractFile::addTagUnique_appends()
{
    TestFile file;
    file.addTagUnique(Tag(FT_SOURCES, uint32{10}));
    file.addTagUnique(Tag(FT_FILENAME, QStringLiteral("test")));
    QCOMPARE(file.tags().size(), std::size_t{2});
}

void tst_AbstractFile::deleteTag_basic()
{
    TestFile file;
    file.setIntTagValue(FT_SOURCES, 42);
    QCOMPARE(file.tags().size(), std::size_t{1});

    file.deleteTag(FT_SOURCES);
    QCOMPARE(file.tags().size(), std::size_t{0});
    QCOMPARE(file.getIntTagValue(FT_SOURCES), uint32{0});
}

void tst_AbstractFile::clearTags_basic()
{
    TestFile file;
    file.setIntTagValue(FT_SOURCES, 1);
    file.setStrTagValue(FT_FILENAME, QStringLiteral("test"));
    QCOMPARE(file.tags().size(), std::size_t{2});

    file.clearTags();
    QVERIFY(file.tags().empty());
}

void tst_AbstractFile::copyTags_basic()
{
    TestFile src;
    src.setIntTagValue(FT_SOURCES, 42);
    src.setStrTagValue(FT_FILENAME, QStringLiteral("hello"));

    TestFile dst;
    dst.copyTags(src.tags());
    QCOMPARE(dst.tags().size(), std::size_t{2});
    QCOMPARE(dst.getIntTagValue(FT_SOURCES), uint32{42});
    QCOMPARE(dst.getStrTagValue(FT_FILENAME), QStringLiteral("hello"));
}

void tst_AbstractFile::tagByName_int()
{
    TestFile file;
    QByteArray name("myTag");
    file.addTagUnique(Tag(name, uint32{123}));
    QCOMPARE(file.getIntTagValue(name), uint32{123});
}

void tst_AbstractFile::tagByName_int64()
{
    TestFile file;
    QByteArray name("myTag64");
    file.addTagUnique(Tag(name, uint64{0xFFFFFFFFFFULL}));
    QCOMPARE(file.getInt64TagValue(name), uint64{0xFFFFFFFFFFULL});
}

void tst_AbstractFile::tagByName_str()
{
    TestFile file;
    QByteArray name("myStrTag");
    file.addTagUnique(Tag(name, QStringLiteral("hello world")));
    QCOMPARE(file.getStrTagValue(name), QStringLiteral("hello world"));
}

void tst_AbstractFile::rating_and_comment()
{
    TestFile file;
    QVERIFY(!file.hasRating());
    QVERIFY(!file.hasBadRating());
    QCOMPARE(file.userRating(), uint32{0});

    file.setUserRating(3);
    QVERIFY(file.hasRating());
    QVERIFY(!file.hasBadRating());
    QCOMPARE(file.userRating(), uint32{3});

    file.setUserRating(1);
    QVERIFY(file.hasBadRating());

    file.setHasComment(true);
    QVERIFY(file.hasComment());

    // loadComment is stubbed; getFileComment should not crash
    QString comment = file.getFileComment();
    QVERIFY(comment.isEmpty());
}

void tst_AbstractFile::kadCommentSearchRunning()
{
    TestFile file;
    QVERIFY(!file.isKadCommentSearchRunning());

    file.setKadCommentSearchRunning(true);
    QVERIFY(file.isKadCommentSearchRunning());
    QCOMPARE(file.updateCallCount, 1);

    // When active, userRating with kadSearchIndicator returns 6
    file.setUserRating(3);
    QCOMPARE(file.userRating(true), uint32{6});
    QCOMPARE(file.userRating(false), uint32{3});

    file.setKadCommentSearchRunning(false);
    QCOMPARE(file.updateCallCount, 2);
    QCOMPARE(file.userRating(true), uint32{3});
}

void tst_AbstractFile::getFileTypeByName_basic()
{
    QCOMPARE(eMule::getFileTypeByName(QStringLiteral("test.mp3")),
             QStringLiteral(ED2KFTSTR_AUDIO));
    QCOMPARE(eMule::getFileTypeByName(QStringLiteral("test.avi")),
             QStringLiteral(ED2KFTSTR_VIDEO));
    QCOMPARE(eMule::getFileTypeByName(QStringLiteral("test.jpg")),
             QStringLiteral(ED2KFTSTR_IMAGE));
    QCOMPARE(eMule::getFileTypeByName(QStringLiteral("test.doc")),
             QStringLiteral(ED2KFTSTR_DOCUMENT));
    QCOMPARE(eMule::getFileTypeByName(QStringLiteral("test.exe")),
             QStringLiteral(ED2KFTSTR_PROGRAM));
    QCOMPARE(eMule::getFileTypeByName(QStringLiteral("test.zip")),
             QStringLiteral(ED2KFTSTR_ARCHIVE));
    QCOMPARE(eMule::getFileTypeByName(QStringLiteral("test.iso")),
             QStringLiteral(ED2KFTSTR_CDIMAGE));
    QVERIFY(eMule::getFileTypeByName(QStringLiteral("test.xyz")).isEmpty());
}

QTEST_MAIN(tst_AbstractFile)
#include "tst_AbstractFile.moc"

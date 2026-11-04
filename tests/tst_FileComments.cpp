/// @file tst_FileComments.cpp
/// @brief ops::setFileComments — the daemon's SetFileComment, one file or a batch (#123).
///
/// MFC CCommentDialog::OnApply (CommentDialog.cpp:166-184): unshared files are
/// skipped, and on a multi-selection whose files disagreed an empty text and a
/// rating of -1 leave each file's own value.

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "app/CoreOps.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <memory>

using namespace eMule;
using namespace Qt::StringLiterals;

class tst_FileComments : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void singleFileSetsBoth();
    void batchSetsEveryFile();
    void keepEmptyLeavesEachComment();
    void ratingMinusOneLeavesEachRating();
    void emptyWithoutKeepClearsAll();
    void unsharedFilesAreSkipped();
    void noSharedFileIs404();
    void badInputIsRejectedBeforeAnyWrite();
    void commentIsCutToTheWireLimit();

private:
    /// A shared file (or only a known one with @p shared false) with hash byte @p b.
    KnownFile* addFile(uint8 b, const QString& comment, uint32 rating, bool shared = true);
    [[nodiscard]] static QString hexOf(uint8 b);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<KnownFileList> m_known;
    std::unique_ptr<SharedFileList> m_shared;
    SharedFileList* m_savedShared = nullptr;
    QString m_savedConfig;
};

void tst_FileComments::init()
{
    // setFileComment writes fileinfo.ini under the config dir; keep it out of the real one
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_savedConfig = thePrefs.configDir();
    thePrefs.setConfigDir(m_dir->path());

    m_known = std::make_unique<KnownFileList>();
    m_shared = std::make_unique<SharedFileList>(m_known.get());
    m_savedShared = theApp.sharedFileList;
    theApp.sharedFileList = m_shared.get();
}

void tst_FileComments::cleanup()
{
    theApp.sharedFileList = m_savedShared;
    m_shared.reset();
    m_known.reset();
    thePrefs.setConfigDir(m_savedConfig);
    m_dir.reset();
}

QString tst_FileComments::hexOf(uint8 b)
{
    uint8 hash[16];
    std::memset(hash, b, 16);
    return encodeBase16(std::span<const uint8>(hash, 16));
}

KnownFile* tst_FileComments::addFile(uint8 b, const QString& comment, uint32 rating, bool shared)
{
    uint8 hash[16];
    std::memset(hash, b, 16);
    auto* file = new KnownFile();
    file->setFileHash(hash);
    file->setFileName(u"file%1.bin"_s.arg(b));
    file->setFileSize(4);
    m_known->safeAddKFile(file);
    if (shared)
        m_shared->safeAddKFile(file);
    file->setFileComment(comment);
    file->setFileRating(rating);
    return file;
}

void tst_FileComments::singleFileSetsBoth()
{
    KnownFile* f = addFile(0x11, u"old"_s, 2);
    int applied = -1;
    QVERIFY(ops::setFileComments({hexOf(0x11)}, u"new"_s, 4, false, &applied).ok());
    QCOMPARE(applied, 1);
    QCOMPARE(f->getFileComment(), u"new"_s);
    QCOMPARE(f->getFileRating(), 4u);
}

void tst_FileComments::batchSetsEveryFile()
{
    KnownFile* a = addFile(0x21, u"ch1"_s, 0);
    KnownFile* b = addFile(0x22, u"ch2"_s, 3);
    KnownFile* c = addFile(0x23, QString(), 5);
    int applied = 0;
    QVERIFY(ops::setFileComments({hexOf(0x21), hexOf(0x22), hexOf(0x23)},
                                 u"same release"_s, 4, true, &applied).ok());
    QCOMPARE(applied, 3);
    for (KnownFile* f : {a, b, c}) {
        QCOMPARE(f->getFileComment(), u"same release"_s);
        QCOMPARE(f->getFileRating(), 4u);
    }
}

void tst_FileComments::keepEmptyLeavesEachComment()
{
    // Files disagreed, user only picked a rating (MFC m_bMergedComment)
    KnownFile* a = addFile(0x31, u"ch1"_s, 1);
    KnownFile* b = addFile(0x32, u"ch2"_s, 2);
    QVERIFY(ops::setFileComments({hexOf(0x31), hexOf(0x32)}, QString(), 5, true).ok());
    QCOMPARE(a->getFileComment(), u"ch1"_s);
    QCOMPARE(b->getFileComment(), u"ch2"_s);
    QCOMPARE(a->getFileRating(), 5u);
    QCOMPARE(b->getFileRating(), 5u);
}

void tst_FileComments::ratingMinusOneLeavesEachRating()
{
    KnownFile* a = addFile(0x41, u"x"_s, 2);
    KnownFile* b = addFile(0x42, u"y"_s, 5);
    QVERIFY(ops::setFileComments({hexOf(0x41), hexOf(0x42)}, u"typed"_s, -1, true).ok());
    QCOMPARE(a->getFileRating(), 2u);
    QCOMPARE(b->getFileRating(), 5u);
    QCOMPARE(a->getFileComment(), u"typed"_s);
    QCOMPARE(b->getFileComment(), u"typed"_s);
}

void tst_FileComments::emptyWithoutKeepClearsAll()
{
    // After Reset the merge is over: the empty text clears every comment
    KnownFile* a = addFile(0x51, u"ch1"_s, 3);
    KnownFile* b = addFile(0x52, u"ch2"_s, 4);
    QVERIFY(ops::setFileComments({hexOf(0x51), hexOf(0x52)}, QString(), 0, false).ok());
    for (KnownFile* f : {a, b}) {
        QVERIFY(f->getFileComment().isEmpty());
        QCOMPARE(f->getFileRating(), 0u);
    }
}

void tst_FileComments::unsharedFilesAreSkipped()
{
    KnownFile* shared = addFile(0x61, QString(), 0);
    KnownFile* known  = addFile(0x62, u"mine"_s, 2, /*shared*/ false);
    int applied = 0;
    QVERIFY(ops::setFileComments({hexOf(0x61), hexOf(0x62), hexOf(0x63) /*unknown*/},
                                 u"good"_s, 5, false, &applied).ok());
    QCOMPARE(applied, 1);
    QCOMPARE(shared->getFileComment(), u"good"_s);
    QCOMPARE(known->getFileComment(), u"mine"_s);
    QCOMPARE(known->getFileRating(), 2u);
}

void tst_FileComments::noSharedFileIs404()
{
    addFile(0x71, u"mine"_s, 2, /*shared*/ false);
    int applied = -1;
    QCOMPARE(ops::setFileComments({hexOf(0x71), hexOf(0x72)}, u"x"_s, 3, false, &applied).code,
             404);
    QCOMPARE(applied, 0);
}

void tst_FileComments::badInputIsRejectedBeforeAnyWrite()
{
    KnownFile* a = addFile(0x81, u"keep"_s, 2);

    // A bad hash late in the list must not leave the first file written
    QCOMPARE(ops::setFileComments({hexOf(0x81), u"nothex"_s}, u"x"_s, 3, false).code, 400);
    QCOMPARE(a->getFileComment(), u"keep"_s);

    QCOMPARE(ops::setFileComments({hexOf(0x81)}, u"x"_s, 6, false).code, 400);
    QCOMPARE(ops::setFileComments({hexOf(0x81)}, u"x"_s, -2, false).code, 400);
    // A wire value that would wrap into range as an int
    QCOMPARE(ops::setFileComments({hexOf(0x81)}, u"x"_s, (qint64{1} << 32) + 3, false).code, 400);
    QCOMPARE(ops::setFileComments({}, u"x"_s, 3, false).code, 400);
    QCOMPARE(a->getFileComment(), u"keep"_s);
    QCOMPARE(a->getFileRating(), 2u);

    theApp.sharedFileList = nullptr;
    QCOMPARE(ops::setFileComments({hexOf(0x81)}, u"x"_s, 3, false).code, 503);
}

void tst_FileComments::commentIsCutToTheWireLimit()
{
    KnownFile* a = addFile(0x91, QString(), 0);
    QVERIFY(ops::setFileComments({hexOf(0x91)}, QString(MAXFILECOMMENTLEN + 20, u'z'), 1,
                                 false).ok());
    QCOMPARE(a->getFileComment().size(), MAXFILECOMMENTLEN);
}

QTEST_GUILESS_MAIN(tst_FileComments)
#include "tst_FileComments.moc"

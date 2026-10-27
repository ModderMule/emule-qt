/// @file tst_MassRename.cpp
/// @brief Mass Rename: mask edits, insert column, Simple cleanup, name checks and the
///        on-disk rename behind it (MorphXT MassRename.cpp / SimpleCleanup.cpp).

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "app/CoreOps.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "utils/MassRename.h"
#include "utils/OtherFunctions.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

using namespace eMule;
using namespace Qt::StringLiterals;
using namespace eMule::massrename;

class tst_MassRename : public QObject {
    Q_OBJECT

private slots:
    void mask_insertLeft();
    void mask_insertRight();
    void mask_replaceSelection();
    void mask_backspaceAndDelete();
    void mask_deleteRight();
    void mask_shortLinesUntouched();
    void insertColumn_left();
    void insertColumn_right();
    void insertColumn_stopsWhenClipboardEnds();
    void simpleCleanup_keepsExtensionDot();
    void simpleCleanup_apostropheAndHex();
    void simpleCleanup_searchReplace();
    void simpleCleanup_charTable();
    void validate_rejectsBadNames();
    void validate_duplicatesPerDirectory();
    void validate_notEnoughLines();
    void renameSharedFile_onDisk();
};

namespace {

[[nodiscard]] MaskEdit edit(const QString& before, qsizetype s1, qsizetype e1,
                            const QString& after, qsizetype s2)
{
    return MaskEdit{s1, e1, s2, s2, before, after};
}

} // namespace

// Typing "X" at column 2 of the mask lands at column 2 of every name
void tst_MassRename::mask_insertLeft()
{
    const QStringList in{u"abcdef.avi"_s, u"uvwxyz.mkv"_s};
    const auto out = applyMaskEdit(in, edit(in[0], 2, 2, u"abXcdef.avi"_s, 3), false);
    QCOMPARE(out, (QStringList{u"abXcdef.avi"_s, u"uvXwxyz.mkv"_s}));
}

// Right-justified: the column counts from the end, so names of any length line up at the extension
void tst_MassRename::mask_insertRight()
{
    const QStringList in{u"short.avi"_s, u"much longer.avi"_s};
    // "_x" typed just before ".avi" of the first line
    const auto out = applyMaskEdit(in, edit(in[0], 5, 5, u"short_x.avi"_s, 7), true);
    QCOMPARE(out, (QStringList{u"short_x.avi"_s, u"much longer_x.avi"_s}));
}

void tst_MassRename::mask_replaceSelection()
{
    const QStringList in{u"01 Song.mp3"_s, u"02 Tune.mp3"_s};
    // "01" selected and typed over with "A"
    const auto out = applyMaskEdit(in, edit(in[0], 0, 2, u"A Song.mp3"_s, 1), false);
    QCOMPARE(out, (QStringList{u"A Song.mp3"_s, u"A Tune.mp3"_s}));
}

void tst_MassRename::mask_backspaceAndDelete()
{
    const QStringList in{u"abcdef"_s, u"uvwxyz"_s};
    // Backspace at 3 removes column 2
    auto out = applyMaskEdit(in, edit(in[0], 3, 3, u"abdef"_s, 2), false);
    QCOMPARE(out, (QStringList{u"abdef"_s, u"uvxyz"_s}));
    // Del at 0 removes column 0
    out = applyMaskEdit(in, edit(in[0], 0, 0, u"bcdef"_s, 0), false);
    QCOMPARE(out, (QStringList{u"bcdef"_s, u"vwxyz"_s}));
    // Ctrl+Backspace of a word takes as many chars as left the mask
    out = applyMaskEdit(in, edit(in[0], 3, 3, u"def"_s, 0), false);
    QCOMPARE(out, (QStringList{u"def"_s, u"xyz"_s}));
}

void tst_MassRename::mask_deleteRight()
{
    const QStringList in{u"name1.avi"_s, u"other1.avi"_s};
    // Backspace before ".avi" on the first line drops the "1" on both
    const auto out = applyMaskEdit(in, edit(in[0], 5, 5, u"name.avi"_s, 4), true);
    QCOMPARE(out, (QStringList{u"name.avi"_s, u"other.avi"_s}));
}

void tst_MassRename::mask_shortLinesUntouched()
{
    const QStringList in{u"abcdefgh"_s, u"ab"_s};
    // Insert at column 5: the second line is too short
    auto out = applyMaskEdit(in, edit(in[0], 5, 5, u"abcdeXfgh"_s, 6), false);
    QCOMPARE(out, (QStringList{u"abcdeXfgh"_s, u"ab"_s}));
    // Delete at column 5
    out = applyMaskEdit(in, edit(in[0], 5, 5, u"abcdegh"_s, 5), false);
    QCOMPARE(out, (QStringList{u"abcdegh"_s, u"ab"_s}));
}

void tst_MassRename::insertColumn_left()
{
    const QStringList in{u"a.avi"_s, u"b.avi"_s, u"c.avi"_s};
    const auto out = massrename::insertColumn(in, u"01 \r\n02 \r\n"_s, 0, 0, 0, 5, false);
    // The trailing newline gives line 3 an empty insert, as in MorphXT
    QCOMPARE(out, (QStringList{u"01 a.avi"_s, u"02 b.avi"_s, u"c.avi"_s}));
}

void tst_MassRename::insertColumn_right()
{
    const QStringList in{u"x.avi"_s, u"long.avi"_s};
    // Caret before ".avi" (column 1 of a 5-char line), selection replaces nothing
    const auto out = massrename::insertColumn(in, u"-1\n-2"_s, 0, 1, 1, 5, true);
    QCOMPARE(out, (QStringList{u"x-1.avi"_s, u"long-2.avi"_s}));
}

void tst_MassRename::insertColumn_stopsWhenClipboardEnds()
{
    const QStringList in{u"a"_s, u"b"_s, u"c"_s};
    // From line 1 on, one clipboard line only; selection [0,1) is replaced
    const auto out = massrename::insertColumn(in, u"Z"_s, 1, 0, 1, 1, false);
    QCOMPARE(out, (QStringList{u"a"_s, u"Z"_s, u"c"_s}));
}

void tst_MassRename::simpleCleanup_keepsExtensionDot()
{
    SimpleCleanupOptions opt;
    opt.flags = DotToSpace | UnderlineToSpace;
    QCOMPARE(simpleCleanupFilename(u"My.Holiday_Video.2024.avi"_s, opt), u"My Holiday Video 2024.avi"_s);
    QCOMPARE(simpleCleanupFilename(u"no_extension"_s, opt), u"no extension"_s);
    QCOMPARE(simpleCleanupFilename(QString(), opt), QString());
}

void tst_MassRename::simpleCleanup_apostropheAndHex()
{
    SimpleCleanupOptions opt;
    opt.flags = ReplaceApostrophe | ReplaceHex;
    QCOMPARE(simpleCleanupFilename(u"Don´t `Stop`.mp3"_s, opt), u"Don't 'Stop'.mp3"_s);
    QCOMPARE(simpleCleanupFilename(u"A%20B%2fC%0A.txt"_s, opt), u"A B/C%0A.txt"_s);
}

void tst_MassRename::simpleCleanup_searchReplace()
{
    SimpleCleanupOptions opt;
    opt.flags = SearchAndReplace;
    opt.search = u"ab"_s;
    opt.replace = u"abab"_s;
    // Replaced text is not searched again; case-insensitive by default
    QCOMPARE(simpleCleanupFilename(u"AB-ab.txt"_s, opt), u"abab-abab.txt"_s);
    opt.flags |= SearchCaseSensitive;
    QCOMPARE(simpleCleanupFilename(u"AB-ab.txt"_s, opt), u"AB-abab.txt"_s);
}

void tst_MassRename::simpleCleanup_charTable()
{
    SimpleCleanupOptions opt;
    opt.flags = ReplaceChars;
    opt.searchChars = {u"ä"_s, u"ß"_s};
    opt.replaceChars = {u"ae"_s, u"ss"_s};
    QCOMPARE(simpleCleanupFilename(u"Gräße.txt"_s, opt), u"Graesse.txt"_s);
    // Lists of unequal length do nothing
    opt.replaceChars.removeLast();
    QCOMPARE(simpleCleanupFilename(u"Gräße.txt"_s, opt), u"Gräße.txt"_s);
}

void tst_MassRename::validate_rejectsBadNames()
{
    const QStringList dirs{u"/d"_s, u"/d"_s};
    for (const QString& bad : {u""_s, u"."_s, u".."_s, u"a/b"_s, u"a\\b"_s, u"a:b"_s,
                               u"a*b"_s, u"a?b"_s, u"a\"b"_s, u"a<b"_s, u"a|b"_s, u"a\tb"_s}) {
        const auto r = validateNewNames({u"ok"_s, bad}, dirs);
        QVERIFY2(!r, qPrintable(bad));
        QCOMPARE(r.error().kind, NameError::Invalid);
        QCOMPARE(r.error().line, 2);
    }
    // Trimmed
    const auto r = validateNewNames({u"  a.txt "_s, u"b.txt"_s}, dirs);
    QVERIFY(r);
    QCOMPARE(r->first(), u"a.txt"_s);
}

void tst_MassRename::validate_duplicatesPerDirectory()
{
    // Same name in two directories is fine; case-only clash in one is not
    auto r = validateNewNames({u"x.txt"_s, u"x.txt"_s}, {u"/a"_s, u"/b"_s});
    QVERIFY(r);
    r = validateNewNames({u"one"_s, u"x.txt"_s, u"X.TXT"_s}, {u"/a"_s, u"/a/"_s, u"/a"_s});
    QVERIFY(!r);
    QCOMPARE(r.error().kind, NameError::Duplicate);
    QCOMPARE(r.error().line, 3);
}

void tst_MassRename::validate_notEnoughLines()
{
    auto r = validateNewNames({u"a"_s}, {u"/d"_s, u"/d"_s});
    QVERIFY(!r);
    QCOMPARE(r.error().kind, NameError::NotEnough);
    // Extra lines are ignored
    r = validateNewNames({u"a"_s, u"b"_s, u"c"_s}, {u"/d"_s, u"/d"_s});
    QVERIFY(r);
    QCOMPARE(r->size(), 2);
}

void tst_MassRename::renameSharedFile_onDisk()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto touch = [&](const QString& name) {
        QFile f(dir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("data");
    };
    touch(u"old.bin"_s);
    touch(u"taken.bin"_s);

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    SharedFileList* const saved = theApp.sharedFileList;
    theApp.sharedFileList = &shared;
    const auto restore = qScopeGuard([saved] { theApp.sharedFileList = saved; });

    uint8 hash[16];
    std::memset(hash, 0x5A, 16);
    auto* file = new KnownFile();
    file->setFileHash(hash);
    file->setFileName(u"old.bin"_s);
    file->setFilePath(dir.filePath(u"old.bin"_s));
    file->setFileSize(4);
    QVERIFY(knownFiles.safeAddKFile(file));
    QVERIFY(shared.safeAddKFile(file));
    const QString hex = encodeBase16(std::span<const uint8>(hash, 16));

    // Bad name, existing target
    QCOMPARE(ops::renameSharedFile(hex, u"a/b.bin"_s).code, 400);
    QCOMPARE(ops::renameSharedFile(hex, u"taken.bin"_s).code, 409);
    QVERIFY(QFile::exists(dir.filePath(u"old.bin"_s)));

    // Plain rename
    QVERIFY(ops::renameSharedFile(hex, u" new.bin "_s).ok());
    QCOMPARE(file->fileName(), u"new.bin"_s);
    QCOMPARE(file->filePath(), dir.filePath(u"new.bin"_s));
    QVERIFY(QFile::exists(dir.filePath(u"new.bin"_s)));
    QVERIFY(!QFile::exists(dir.filePath(u"old.bin"_s)));

    // Case only, also on a case-insensitive file system
    QVERIFY(ops::renameSharedFile(hex, u"NEW.bin"_s).ok());
    QCOMPARE(file->fileName(), u"NEW.bin"_s);
    QVERIFY(QDir(dir.path()).entryList(QDir::Files).contains(u"NEW.bin"_s));

    // Mass Rename's dispatcher sends a complete file the same way
    QVERIFY(ops::renameFile(hex, u"via dispatch.bin"_s).ok());
    QVERIFY(QFile::exists(dir.filePath(u"via dispatch.bin"_s)));

    // Unknown hash
    QCOMPARE(ops::renameSharedFile(QString(32, u'0'), u"x"_s).code, 404);
}

QTEST_GUILESS_MAIN(tst_MassRename)
#include "tst_MassRename.moc"

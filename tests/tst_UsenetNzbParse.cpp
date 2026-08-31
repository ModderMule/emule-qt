/// @file tst_UsenetNzbParse.cpp
/// @brief NZB parsing and the subject heuristics that go with it.
///
/// Two things here are easy to get confidently wrong:
///
///   - **`<segment bytes>` is the ENCODED size.** yEnc overhead plus line
///     breaks. Decoded offsets cannot be derived from an NZB at all — only the
///     article's own `=ypart begin/end` is authoritative. The model says so and
///     the tests assert on it, because using it as an offset produces a file
///     that is subtly wrong rather than obviously broken.
///   - **A subject with no filename and no counter is normal.** Obfuscated
///     posts carry neither; the real name arrives in the first article's
///     `=ybegin name=`. nZEDb keeps dropped-header logs precisely because the
///     regex is never complete. A parser that treats "unparseable subject" as
///     an error rejects a large minority of real posts.
///
/// And one that looks like an edge case but is the commonest real failure: an
/// indexer serving an HTML error page under a .nzb name. That has to fail
/// loudly, or the queue gets a permanently stalled item with no explanation.

#include "nzb/NzbFile.h"
#include "nzb/SubjectParser.h"

#include <QTemporaryDir>
#include <QTest>

using namespace eMule::usenet;

namespace {

/// Defined below the test class on purpose: its body is a raw string
/// literal, and moc mis-parses one that appears above a Q_OBJECT class,
/// emitting an empty .moc and a "missing vtable" link error.
QByteArray sampleNzb();

} // namespace

class tst_UsenetNzbParse : public QObject {
    Q_OBJECT

private slots:
    void parsesFilesGroupsAndSegments();
    void segmentBytesAreEncodedSize();
    void messageIdBracketsAreStripped();
    void passwordComesFromMetaTag();
    void namespaceIsIgnored();
    void htmlErrorPageIsRejected();
    void malformedXmlIsRejected();
    void fileWithNoSegmentsIsSkipped();
    void completenessNeedsEveryPart();
    void par2FilesAreRecognised();
    void passwordFromFileName();
    void parseFileSeedsTheName();

    void subject_quotedNameWins();
    void subject_bareNameIsFound();
    void subject_obfuscatedYieldsNothing();
    void subject_partCounterIsNotEndAnchored();
    void subject_yearInTitleIsNotAPartCounter();
};

namespace {

QByteArray sampleNzb()
{
    return QByteArrayLiteral(R"NZB(<?xml version="1.0" encoding="iso-8859-1" ?>
<!DOCTYPE nzb PUBLIC "-//newzBin//DTD NZB 1.1//EN" "http://www.newzbin.com/DTD/nzb/nzb-1.1.dtd">
<nzb xmlns="http://www.newzbin.com/DTD/2003/nzb">
  <head>
    <meta type="password">letmein</meta>
    <meta type="tag">stuff</meta>
  </head>
  <file poster="Joe &lt;joe@example.com&gt;" date="1710000000" subject="[1/3] - &quot;Some.Release.part01.rar&quot; yEnc (1/2)">
    <groups>
      <group>alt.binaries.test</group>
      <group>alt.binaries.other</group>
    </groups>
    <segments>
      <segment bytes="716800" number="1">part1a@example.com</segment>
      <segment bytes="500000" number="2">&lt;part1b@example.com&gt;</segment>
    </segments>
  </file>
  <file poster="Joe" date="1710000001" subject="[2/3] - &quot;Some.Release.vol00+01.par2&quot; yEnc (1/1)">
    <groups><group>alt.binaries.test</group></groups>
    <segments>
      <segment bytes="120000" number="1">par1@example.com</segment>
    </segments>
  </file>
</nzb>
)NZB");
}

} // namespace

void tst_UsenetNzbParse::parsesFilesGroupsAndSegments()
{
    NzbInfo info;
    QString error;
    QVERIFY2(NzbFile::parse(sampleNzb(), info, error), qPrintable(error));

    QCOMPARE(info.files.size(), 2);
    QCOMPARE(info.segmentCount(), 3);

    const NzbFileInfo& first = info.files.at(0);
    QCOMPARE(first.fileName, QStringLiteral("Some.Release.part01.rar"));
    QCOMPARE(first.poster, QStringLiteral("Joe <joe@example.com>"));
    QCOMPARE(first.date, Q_INT64_C(1710000000));
    QCOMPARE(first.groups, QStringList({QStringLiteral("alt.binaries.test"),
                                        QStringLiteral("alt.binaries.other")}));
    QCOMPARE(first.segments.size(), 2);
    QCOMPARE(first.segments.at(0).number, 1);
    QCOMPARE(first.partsTotal, 2);   // the "(1/2)" counter
}

void tst_UsenetNzbParse::segmentBytesAreEncodedSize()
{
    NzbInfo info;
    QString error;
    QVERIFY(NzbFile::parse(sampleNzb(), info, error));

    // Encoded, not decoded. Good as a progress denominator; useless as an
    // offset, which is the mistake this assertion exists to pin down.
    QCOMPARE(info.files.at(0).segments.at(0).bytes, Q_INT64_C(716800));
    QCOMPARE(info.files.at(0).encodedBytes(), Q_INT64_C(1216800));
    QCOMPARE(info.totalEncodedBytes(), Q_INT64_C(1336800));
}

void tst_UsenetNzbParse::messageIdBracketsAreStripped()
{
    NzbInfo info;
    QString error;
    QVERIFY(NzbFile::parse(sampleNzb(), info, error));

    // NZBs store ids both ways. Keeping the brackets here would put "<<id>>"
    // on the wire, and the server answers 430 with no hint why.
    QCOMPARE(info.files.at(0).segments.at(0).messageId,
             QStringLiteral("part1a@example.com"));
    QCOMPARE(info.files.at(0).segments.at(1).messageId,
             QStringLiteral("part1b@example.com"));
}

void tst_UsenetNzbParse::passwordComesFromMetaTag()
{
    NzbInfo info;
    QString error;
    QVERIFY(NzbFile::parse(sampleNzb(), info, error));
    QCOMPARE(info.password, QStringLiteral("letmein"));
}

void tst_UsenetNzbParse::namespaceIsIgnored()
{
    // Real NZBs carry the newzbin namespace, a wrong one, or none. Refusing any
    // of those helps nobody, so matching is on the local name.
    const QByteArray noNamespace = QByteArrayLiteral(
        "<nzb><file subject=\"&quot;a.rar&quot; (1/1)\">"
        "<segments><segment bytes=\"10\" number=\"1\">x@y</segment></segments>"
        "</file></nzb>");

    NzbInfo info;
    QString error;
    QVERIFY2(NzbFile::parse(noNamespace, info, error), qPrintable(error));
    QCOMPARE(info.files.size(), 1);
    QCOMPARE(info.files.at(0).fileName, QStringLiteral("a.rar"));
}

void tst_UsenetNzbParse::htmlErrorPageIsRejected()
{
    // The commonest real failure: an indexer out of API calls serves HTML under
    // a .nzb name. Well-formed, and completely undownloadable.
    const QByteArray html = QByteArrayLiteral(
        "<html><body><h1>API limit reached</h1></body></html>");

    NzbInfo info;
    QString error;
    QVERIFY(!NzbFile::parse(html, info, error));
    QVERIFY(!error.isEmpty());
    QVERIFY(info.files.isEmpty());
}

void tst_UsenetNzbParse::malformedXmlIsRejected()
{
    NzbInfo info;
    QString error;
    QVERIFY(!NzbFile::parse(QByteArrayLiteral("<nzb><file></nzb>"), info, error));
    QVERIFY(!error.isEmpty());
}

void tst_UsenetNzbParse::fileWithNoSegmentsIsSkipped()
{
    const QByteArray nzb = QByteArrayLiteral(
        "<nzb>"
        "<file subject=\"&quot;empty.rar&quot;\"><segments></segments></file>"
        "<file subject=\"&quot;real.rar&quot;\">"
        "<segments><segment bytes=\"10\" number=\"1\">x@y</segment></segments></file>"
        "</nzb>");

    NzbInfo info;
    QString error;
    QVERIFY(NzbFile::parse(nzb, info, error));
    // A file with no articles is not downloadable; carrying it would show a
    // permanently 0% row in the queue.
    QCOMPARE(info.files.size(), 1);
    QCOMPARE(info.files.at(0).fileName, QStringLiteral("real.rar"));
}

void tst_UsenetNzbParse::completenessNeedsEveryPart()
{
    NzbFileInfo file;
    file.partsTotal = 3;
    file.segments = {{QStringLiteral("a"), 10, 1},
                     {QStringLiteral("b"), 10, 2},
                     {QStringLiteral("c"), 10, 3}};
    QVERIFY(file.hasAllSegments());

    // A duplicate number passes a size check while leaving a hole -- which is
    // why the check walks the sorted numbers rather than counting.
    file.segments[2].number = 2;
    QVERIFY(!file.hasAllSegments());

    file.segments.removeLast();
    QVERIFY(!file.hasAllSegments());

    // With no counter in the subject, "has any segment" is the most that can
    // honestly be said.
    file.partsTotal = 0;
    QVERIFY(file.hasAllSegments());
}

void tst_UsenetNzbParse::par2FilesAreRecognised()
{
    NzbInfo info;
    QString error;
    QVERIFY(NzbFile::parse(sampleNzb(), info, error));
    QVERIFY(!info.files.at(0).isPar2());
    QVERIFY(info.files.at(1).isPar2());

    // Obfuscated post: no filename, but the subject still carries the token.
    NzbFileInfo obfuscated;
    obfuscated.subject = QStringLiteral("xy8Kd - (1/9) .par2");
    QVERIFY(obfuscated.isPar2());
}

void tst_UsenetNzbParse::passwordFromFileName()
{
    QString name = QStringLiteral("Some.Release{{hunter2}}");
    QCOMPARE(NzbFile::takePasswordFromName(name), QStringLiteral("hunter2"));
    QCOMPARE(name, QStringLiteral("Some.Release"));

    QString plain = QStringLiteral("Some.Release");
    QVERIFY(NzbFile::takePasswordFromName(plain).isEmpty());
    QCOMPARE(plain, QStringLiteral("Some.Release"));

    // An unterminated marker is left alone rather than half-consumed.
    QString broken = QStringLiteral("Some.Release{{oops");
    QVERIFY(NzbFile::takePasswordFromName(broken).isEmpty());
    QCOMPARE(broken, QStringLiteral("Some.Release{{oops"));
}

void tst_UsenetNzbParse::parseFileSeedsTheName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("My.Release{{fromname}}.nzb"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(sampleNzb());
    }

    NzbInfo info;
    QString error;
    QVERIFY2(NzbFile::parseFile(path, info, error), qPrintable(error));
    QCOMPARE(info.name, QStringLiteral("My.Release"));
    // The meta tag wins over the filename: the filename is a convention, the
    // meta tag is the format saying so.
    QCOMPARE(info.password, QStringLiteral("letmein"));
}

// ---------------------------------------------------------------------------
// Subject heuristics
// ---------------------------------------------------------------------------

void tst_UsenetNzbParse::subject_quotedNameWins()
{
    const auto info = parseSubject(
        QStringLiteral(R"([1/8] - "Some.Release.r00" yEnc (3/97))"));
    QCOMPARE(info.fileName, QStringLiteral("Some.Release.r00"));
    QCOMPARE(info.part, 3);
    QCOMPARE(info.total, 97);
    QCOMPARE(info.fileIndex, 1);
    QCOMPARE(info.fileTotal, 8);
}

void tst_UsenetNzbParse::subject_bareNameIsFound()
{
    const auto info = parseSubject(QStringLiteral("Some.Release.part02.rar (12/97)"));
    QCOMPARE(info.fileName, QStringLiteral("Some.Release.part02.rar"));
    QCOMPARE(info.part, 12);
    QCOMPARE(info.total, 97);
}

void tst_UsenetNzbParse::subject_obfuscatedYieldsNothing()
{
    // Normal, not an error. The real name only appears in the first article's
    // "=ybegin name=", and the caller is required to cope with that.
    const auto info = parseSubject(QStringLiteral("[3/9] xw8Ks92m1 - (2/181)"));
    QVERIFY(info.fileName.isEmpty());
    QCOMPARE(info.part, 2);
    QCOMPARE(info.total, 181);
}

void tst_UsenetNzbParse::subject_partCounterIsNotEndAnchored()
{
    // Real subjects append junk after the counter. Anchoring the regex at the
    // end silently drops a large minority of posts -- nZEDb keeps dropped-header
    // logs because of exactly this.
    const auto info = parseSubject(
        QStringLiteral(R"("x.rar" yEnc (5/20) 716800 bytes [trailing junk])"));
    QCOMPARE(info.part, 5);
    QCOMPARE(info.total, 20);
    QCOMPARE(info.fileName, QStringLiteral("x.rar"));
}

void tst_UsenetNzbParse::subject_yearInTitleIsNotAPartCounter()
{
    // "(2011)" has no slash so it cannot match; taking the *last* counter is
    // what keeps a genuine "(1/9)" from losing to something earlier in a title.
    const auto info = parseSubject(
        QStringLiteral(R"(Some Movie (2011) - "sm.part1.rar" yEnc (1/9))"));
    QCOMPARE(info.part, 1);
    QCOMPARE(info.total, 9);
    QCOMPARE(info.fileName, QStringLiteral("sm.part1.rar"));
}

QTEST_MAIN(tst_UsenetNzbParse)
#include "tst_UsenetNzbParse.moc"

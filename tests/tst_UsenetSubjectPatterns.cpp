/// @file tst_UsenetSubjectPatterns.cpp
/// @brief The subject heuristics, once they became data.
///
/// Split out of tst_UsenetNzbParse for one reason: two of these cases write to
/// thePrefs, which is a process global. Left in that file they would couple
/// every unrelated NZB case to test order.
///
/// The centre of it is defaultsReproduceTheOldHeuristicExactly, which asserts
/// the new rule engine against a **frozen verbatim copy** of the hand-written
/// parser this replaced. That copy is a fixture, not code under maintenance:
///
///   ⚠️ Do not update legacyParseSubject() to follow the new implementation.
///   The day the two disagree is the day this test is doing its job, and the
///   disagreement is a decision somebody has to make, not a merge conflict.

#include "TestHelpers.h"

#include "nzb/SubjectParser.h"
#include "prefs/Preferences.h"

#include <QRegularExpression>
#include <QTest>

using namespace eMule;
using namespace eMule::usenet;

namespace {

UsenetSubjectPattern rule(const QString& name, UsenetSubjectRole role, const QString& pattern,
                          UsenetSubjectPick pick = UsenetSubjectPick::First,
                          bool caseInsensitive = false, bool enabled = true)
{
    return {name, role, pattern, pick, caseInsensitive, enabled};
}

SubjectInfo legacyParseSubject(const QString& subject);

} // namespace

class tst_UsenetSubjectPatterns : public QObject {
    Q_OBJECT

private slots:
    void cleanup();

    void defaultsReproduceTheOldHeuristicExactly_data();
    void defaultsReproduceTheOldHeuristicExactly();

    void aPatternThatWillNotCompileIsSkippedRatherThanFatal();
    void aRoleWhoseRulesAllFailToCompileFallsBackToTheBuiltIns();
    void aRuleWithPositionalGroupsInsteadOfNamedOnesIsSkipped();
    void aCounterRuleMissingTotalIsSkippedSoPartNeverArrivesWithoutIt();
    void aRuleThatMatchesTheEmptyStringIsRefused();
    void configuringOneRoleLeavesTheOthersOnTheirBuiltIns();
    void theFirstRuleThatMatchesWinsAndTheRestAreNotTried();
    void aQuotedRuleThatMatchesOnlyWhitespaceStillWinsOutright();
    void pickLastIsWhatKeepsAYearFromWinningThePartCounter();
    void aDisabledRuleTurnsItsRoleOffRatherThanRestoringTheBuiltIn();
    void anAbsurdlyLongSubjectYieldsNothingRatherThanHanging();
    void theSameRuleSetIsHandedOutUntilThePreferenceChanges();
};

void tst_UsenetSubjectPatterns::cleanup()
{
    thePrefs.setUsenetSubjectPatterns({});
}

// ---------------------------------------------------------------------------
// Equivalence
// ---------------------------------------------------------------------------

void tst_UsenetSubjectPatterns::defaultsReproduceTheOldHeuristicExactly_data()
{
    QTest::addColumn<QString>("subject");
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("part");
    QTest::addColumn<int>("total");
    QTest::addColumn<int>("fileIndex");
    QTest::addColumn<int>("fileTotal");
    QTest::addColumn<bool>("matchesLegacy");

    const auto row = [](const char* tag, const QString& subject, const QString& name,
                        int part, int total, int fileIndex, int fileTotal,
                        bool matchesLegacy = true) {
        QTest::newRow(tag) << subject << name << part << total
                           << fileIndex << fileTotal << matchesLegacy;
    };

    row("canonical", QStringLiteral(R"([1/8] - "Some.Release.r00" yEnc (03/97))"),
        QStringLiteral("Some.Release.r00"), 3, 97, 1, 8);
    row("bare name", QStringLiteral("Some.Release.part02.rar (12/97)"),
        QStringLiteral("Some.Release.part02.rar"), 12, 97, 0, 0);
    row("obfuscated with par2",
        QStringLiteral(R"(abcdef0123456789 [04/25] - "abcdef0123456789.vol00+01.par2" yEnc (1/9))"),
        QStringLiteral("abcdef0123456789.vol00+01.par2"), 1, 9, 4, 25);
    row("no filename at all", QStringLiteral("[3/9] xw8Ks92m1 - (2/181)"),
        QString(), 2, 181, 3, 9);
    row("year in title", QStringLiteral(R"(Some Movie (2011) - "sm.part1.rar" yEnc (1/9))"),
        QStringLiteral("sm.part1.rar"), 1, 9, 0, 0);
    row("trailing junk", QStringLiteral(R"("x.rar" yEnc (5/20) 716800 bytes [trailing junk])"),
        QStringLiteral("x.rar"), 5, 20, 0, 0);
    row("two bare names, last wins",
        QStringLiteral("Some.Release.rar - Other.Release.nfo (1/2)"),
        QStringLiteral("Other.Release.nfo"), 1, 2, 0, 0);
    row("two quoted, first wins", QStringLiteral(R"(a "b" c "d" (1/2))"),
        QStringLiteral("b"), 1, 2, 0, 0);
    row("split archive", QStringLiteral("Some.Release.7z.001 (1/5)"),
        QStringLiteral("Some.Release.7z.001"), 1, 5, 0, 0);
    row("no counter", QStringLiteral(R"("Some.Release.nfo" yEnc)"),
        QStringLiteral("Some.Release.nfo"), 0, 0, 0, 0);
    row("file counter only", QStringLiteral("[1/9] - just a description"),
        QString(), 0, 0, 1, 9);
    row("six digit total is not a counter", QStringLiteral(R"("name.rar" (1/123456))"),
        QStringLiteral("name.rar"), 0, 0, 0, 0);
    row("non latin", QStringLiteral(R"(日本語のタイトル - "movie.mkv" yEnc (2/7))"),
        QStringLiteral("movie.mkv"), 2, 7, 0, 0);

    // The quirk: the quoted rule wins outright even though what it captured
    // trims to nothing, so the bare-name rule never runs. Preserved, not fixed.
    row("quoted whitespace wins and yields nothing",
        QStringLiteral(R"(Some.Release.rar " " yEnc (1/2))"), QString(), 1, 2, 0, 0);

    row("empty", QString(), QString(), 0, 0, 0, 0);

    // The one intentional divergence: the legacy parser had no length cap
    // because it had no user-supplied patterns to protect against.
    row("absurdly long",
        QStringLiteral(R"("x.rar" )") + QString(4000, QLatin1Char('a')) + QStringLiteral(" (1/2)"),
        QString(), 0, 0, 0, 0, /*matchesLegacy*/ false);
}

void tst_UsenetSubjectPatterns::defaultsReproduceTheOldHeuristicExactly()
{
    QFETCH(QString, subject);
    QFETCH(QString, name);
    QFETCH(int, part);
    QFETCH(int, total);
    QFETCH(int, fileIndex);
    QFETCH(int, fileTotal);
    QFETCH(bool, matchesLegacy);

    SubjectInfo expected;
    expected.fileName = name;
    expected.part = part;
    expected.total = total;
    expected.fileIndex = fileIndex;
    expected.fileTotal = fileTotal;

    const SubjectInfo actual = parseSubject(subject);
    QCOMPARE(actual, expected);

    // Both: the frozen copy proves equivalence at the moment of the change, the
    // golden columns keep the intent readable after it is eventually deleted.
    if (matchesLegacy)
        QCOMPARE(actual, legacyParseSubject(subject));
}

// ---------------------------------------------------------------------------
// Policy
// ---------------------------------------------------------------------------

void tst_UsenetSubjectPatterns::aPatternThatWillNotCompileIsSkippedRatherThanFatal()
{
    // The opposite of IndexerFeedMatch's rule, deliberately: there a broken
    // reject pattern lets everything through and spends money unattended, so the
    // safe direction is to stop. Here the worst outcome is no filename, which is
    // a documented normal answer -- and failing closed would stop every NZB in
    // the queue over one typo, at parse time, with nowhere to show why.
    const auto set = SubjectRuleSet::compile({
        rule(QStringLiteral("broken"), UsenetSubjectRole::Name, QStringLiteral("(?<name>[")),
        rule(QStringLiteral("good"), UsenetSubjectRole::Name,
             QStringLiteral(R"(\b(?<name>\w+\.mkv)\b)")),
    });

    const SubjectInfo info = set.parse(QStringLiteral("junk movie.mkv (1/2)"));
    QCOMPARE(info.fileName, QStringLiteral("movie.mkv"));
    // The roles nobody configured still work.
    QCOMPARE(info.part, 1);
    QCOMPARE(info.total, 2);
}

void tst_UsenetSubjectPatterns::aRoleWhoseRulesAllFailToCompileFallsBackToTheBuiltIns()
{
    // A configuration that is entirely typos should behave like no configuration
    // at all, because that is the state a user can reason about and reproduce.
    const auto set = SubjectRuleSet::compile({
        rule(QStringLiteral("broken"), UsenetSubjectRole::Name, QStringLiteral("(?<name>[")),
    });

    QCOMPARE(set.parse(QStringLiteral(R"("Some.Release.r00" yEnc (3/97))")).fileName,
             QStringLiteral("Some.Release.r00"));
}

void tst_UsenetSubjectPatterns::aRuleWithPositionalGroupsInsteadOfNamedOnesIsSkipped()
{
    // The silent-wrong-answer guard. With positional groups a user who wraps an
    // alternation shifts group 1 and 2 by one, and the parser then reports
    // "part 97 of 3" -- and this component has no error channel to say so on.
    const auto set = SubjectRuleSet::compile({
        rule(QStringLiteral("positional"), UsenetSubjectRole::Part,
             QStringLiteral(R"(\((\d+)/(\d+)\))")),
    });

    // Skipped, so the built-in part rule stands and the answer is still right.
    const SubjectInfo info = set.parse(QStringLiteral("x (3/97)"));
    QCOMPARE(info.part, 3);
    QCOMPARE(info.total, 97);
}

void tst_UsenetSubjectPatterns::aCounterRuleMissingTotalIsSkippedSoPartNeverArrivesWithoutIt()
{
    // "Part 3 of unknown" is a state nothing downstream has ever seen, and
    // partsTotal feeds the completeness check.
    const auto set = SubjectRuleSet::compile({
        rule(QStringLiteral("half"), UsenetSubjectRole::Part,
             QStringLiteral(R"(part (?<index>\d+))")),
    });

    const SubjectInfo info = set.parse(QStringLiteral("part 3 of something"));
    QCOMPARE(info.part, 0);
    QCOMPARE(info.total, 0);
}

void tst_UsenetSubjectPatterns::aRuleThatMatchesTheEmptyStringIsRefused()
{
    const auto set = SubjectRuleSet::compile({
        rule(QStringLiteral("everything"), UsenetSubjectRole::Name,
             QStringLiteral("(?<name>.*)")),
    });

    // Refused, so the built-in name rules stand rather than every subject
    // answering with its whole self.
    QCOMPARE(set.parse(QStringLiteral(R"("real.mkv" yEnc)")).fileName,
             QStringLiteral("real.mkv"));
}

void tst_UsenetSubjectPatterns::configuringOneRoleLeavesTheOthersOnTheirBuiltIns()
{
    // The realistic edit is "a new scheme appeared, I want one more filename
    // pattern". Under whole-list replacement that user silently loses the part
    // counter and gets a queue of releases with no (n/m).
    const auto set = SubjectRuleSet::compile({
        rule(QStringLiteral("hex"), UsenetSubjectRole::Name,
             QStringLiteral(R"((?<name>[0-9a-f]{16}\.dat))")),
    });

    const SubjectInfo info = set.parse(QStringLiteral("[2/6] abcdef0123456789.dat (7/44)"));
    QCOMPARE(info.fileName, QStringLiteral("abcdef0123456789.dat"));
    QCOMPARE(info.part, 7);
    QCOMPARE(info.total, 44);
    QCOMPARE(info.fileIndex, 2);
    QCOMPARE(info.fileTotal, 6);
}

void tst_UsenetSubjectPatterns::theFirstRuleThatMatchesWinsAndTheRestAreNotTried()
{
    const auto set = SubjectRuleSet::compile({
        rule(QStringLiteral("first"), UsenetSubjectRole::Name,
             QStringLiteral(R"((?<name>\w+\.nfo))")),
        rule(QStringLiteral("second"), UsenetSubjectRole::Name,
             QStringLiteral(R"((?<name>\w+\.mkv))")),
    });

    QCOMPARE(set.parse(QStringLiteral("info.nfo and movie.mkv")).fileName,
             QStringLiteral("info.nfo"));
}

void tst_UsenetSubjectPatterns::aQuotedRuleThatMatchesOnlyWhitespaceStillWinsOutright()
{
    // Shipping behaviour, pinned so the eventual fix is a decision rather than an
    // accident: "wins" means hasMatch(), not "produced a non-empty name".
    QCOMPARE(parseSubject(QStringLiteral(R"(Some.Release.rar " " yEnc (1/2))")).fileName,
             QString());
}

void tst_UsenetSubjectPatterns::pickLastIsWhatKeepsAYearFromWinningThePartCounter()
{
    // Same pattern, both picks, one subject: only the pick differs.
    const QString subject = QStringLiteral("Some Movie (2011) - sm.part1.rar yEnc (1/9)");
    const QString pattern = QStringLiteral(R"(\((?<index>\d{1,5})\s*/\s*(?<total>\d{1,5})\))");

    const auto takesLast = SubjectRuleSet::compile({
        rule(QStringLiteral("counter"), UsenetSubjectRole::Part, pattern,
             UsenetSubjectPick::Last)});
    QCOMPARE(takesLast.parse(subject).part, 1);
    QCOMPARE(takesLast.parse(subject).total, 9);

    // "(2011)" carries no slash, so the trap needs a second real counter to bite.
    const QString twoCounters = QStringLiteral("Some Pack (1/3) - part (7/44)");
    const auto takesFirst = SubjectRuleSet::compile({
        rule(QStringLiteral("counter"), UsenetSubjectRole::Part, pattern,
             UsenetSubjectPick::First)});
    QCOMPARE(takesFirst.parse(twoCounters).part, 1);
    QCOMPARE(takesLast.parse(twoCounters).part, 7);
}

void tst_UsenetSubjectPatterns::aDisabledRuleTurnsItsRoleOffRatherThanRestoringTheBuiltIn()
{
    // Turning a rule off is a decision; a rule that will not compile is a
    // mistake. Only the second one is worth rescuing with the built-ins.
    const auto set = SubjectRuleSet::compile({
        rule(QStringLiteral("off"), UsenetSubjectRole::Name,
             QStringLiteral(R"((?<name>\w+\.mkv))"), UsenetSubjectPick::First,
             /*caseInsensitive*/ false, /*enabled*/ false),
    });

    const SubjectInfo info = set.parse(QStringLiteral(R"("Some.Release.r00" yEnc (3/97))"));
    QCOMPARE(info.fileName, QString());
    QCOMPARE(info.part, 3);      // the roles it did not mention are untouched
}

void tst_UsenetSubjectPatterns::anAbsurdlyLongSubjectYieldsNothingRatherThanHanging()
{
    // The only bound available: Qt exposes no PCRE2 match_limit and no way to
    // interrupt a match, so with user-supplied patterns the input length is the
    // only thing standing between a bad regex and a wedged daemon.
    const QString huge = QStringLiteral(R"("x.rar" )")
                         + QString(kMaxSubjectChars, QLatin1Char('a'))
                         + QStringLiteral(" (1/2)");
    const SubjectInfo info = parseSubject(huge);
    QCOMPARE(info, SubjectInfo{});

    // And one character under the cap is still parsed, so the cap is a cap and
    // not an off-by-one that quietly drops ordinary subjects.
    QString justUnder = QStringLiteral(R"("x.rar" yEnc (1/2))");
    justUnder += QString(kMaxSubjectChars - justUnder.size(), QLatin1Char('z'));
    QCOMPARE(justUnder.size(), int(kMaxSubjectChars));
    QCOMPARE(parseSubject(justUnder).fileName, QStringLiteral("x.rar"));
}

void tst_UsenetSubjectPatterns::theSameRuleSetIsHandedOutUntilThePreferenceChanges()
{
    const auto first = subjectRules();
    QVERIFY(first);
    QCOMPARE(subjectRules().get(), first.get());   // not rebuilt per call

    thePrefs.setUsenetSubjectPatterns({
        rule(QStringLiteral("hex"), UsenetSubjectRole::Name,
             QStringLiteral(R"((?<name>[0-9a-f]{16}\.dat))")),
    });

    const auto second = subjectRules();
    QVERIFY(second.get() != first.get());
    QCOMPARE(second->parse(QStringLiteral("abcdef0123456789.dat (1/2)")).fileName,
             QStringLiteral("abcdef0123456789.dat"));

    // The old set is still valid and still answers the old way -- which is what
    // makes holding one for a whole document safe.
    QCOMPARE(first->parse(QStringLiteral(R"("keep.mkv" (1/2))")).fileName,
             QStringLiteral("keep.mkv"));
}

// ---------------------------------------------------------------------------
// The frozen original, kept below the Q_OBJECT class on purpose
// ---------------------------------------------------------------------------
//
// ⚠️ AUTOMOC parses this file and its parser does not understand raw string
// literals: put one *above* the Q_OBJECT class and moc emits a 0-byte .moc,
// which surfaces as "undefined vtable" at link time with nothing pointing here.

namespace {

// ---------------------------------------------------------------------------
// The frozen original. Copied verbatim from SubjectParser.cpp before the rules
// moved into preferences.yml. Nothing here may be "improved".
// ---------------------------------------------------------------------------

const QRegularExpression& legacyPartCounter()
{
    static const QRegularExpression re(QStringLiteral(R"(\((\d{1,5})\s*/\s*(\d{1,5})\))"));
    return re;
}

const QRegularExpression& legacyFileCounter()
{
    static const QRegularExpression re(QStringLiteral(R"(\[(\d{1,5})\s*/\s*(\d{1,5})\])"));
    return re;
}

const QRegularExpression& legacyQuotedName()
{
    // Escaped rather than a raw string with a custom delimiter: moc parses this
    // file (it has a Q_OBJECT class) and chokes on R"RX(...)RX" with
    // "missing ')' in macro usage".
    static const QRegularExpression re(QStringLiteral("\"([^\"]{1,255})\""));
    return re;
}

const QRegularExpression& legacyBareName()
{
    static const QRegularExpression re(
        QStringLiteral(R"(([^\s"]+\.(?:part\d+\.rar|vol\d+\+\d+\.par2|par2|rar|r\d{2,3}|7z|zip|nfo|sfv|mkv|mp4|avi|iso|\d{3})))"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

QRegularExpressionMatch legacyLastMatch(const QRegularExpression& re, const QString& text)
{
    QRegularExpressionMatch best;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        best = it.next();
    return best;
}

SubjectInfo legacyParseSubject(const QString& subject)
{
    SubjectInfo info;
    if (subject.isEmpty())
        return info;

    if (const auto m = legacyLastMatch(legacyPartCounter(), subject); m.hasMatch()) {
        info.part = m.captured(1).toInt();
        info.total = m.captured(2).toInt();
    }
    if (const auto m = legacyFileCounter().match(subject); m.hasMatch()) {
        info.fileIndex = m.captured(1).toInt();
        info.fileTotal = m.captured(2).toInt();
    }

    if (const auto m = legacyQuotedName().match(subject); m.hasMatch()) {
        info.fileName = m.captured(1).trimmed();
        return info;
    }

    if (const auto m = legacyLastMatch(legacyBareName(), subject); m.hasMatch()) {
        info.fileName = m.captured(1).trimmed();
        return info;
    }

    return info;
}

} // namespace

QTEST_MAIN(tst_UsenetSubjectPatterns)
#include "tst_UsenetSubjectPatterns.moc"

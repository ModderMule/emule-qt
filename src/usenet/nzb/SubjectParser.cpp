#include "nzb/SubjectParser.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

namespace eMule::usenet {

namespace {

/// Last match of @p re in @p text, or a null match.
QRegularExpressionMatch lastMatch(const QRegularExpression& re, const QString& text)
{
    QRegularExpressionMatch best;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        best = it.next();
    return best;
}

int roleIndex(UsenetSubjectRole role) { return static_cast<int>(role); }

} // namespace

// ---------------------------------------------------------------------------
// The defaults
// ---------------------------------------------------------------------------

QList<UsenetSubjectPattern> SubjectRuleSet::builtInPatterns()
{
    return {
        // The yEnc part counter, unanchored at the end: real subjects append
        // "yEnc", byte counts and assorted junk after it, and anchoring is how
        // you silently drop a large minority of posts (nZEDb learned this the
        // hard way and keeps dropped-header logs to prove it).
        //
        // `pick: last` is what distinguishes "(2/181)" from a "(2011)" earlier
        // in a title -- the counter is the last parenthesised pair on the line.
        {QStringLiteral("yenc-part-counter"), UsenetSubjectRole::Part,
         QStringLiteral(R"(\((?<index>\d{1,5})\s*/\s*(?<total>\d{1,5})\))"),
         UsenetSubjectPick::Last, false, true},

        // The file counter, e.g. "[03/12]". Same shape, square brackets.
        {QStringLiteral("bracketed-file-counter"), UsenetSubjectRole::File,
         QStringLiteral(R"(\[(?<index>\d{1,5})\s*/\s*(?<total>\d{1,5})\])"),
         UsenetSubjectPick::First, false, true},

        // A quoted filename wins outright: posters who quote mean it. Listed
        // first, and the first rule that *matches* ends the role -- which is
        // how "wins outright" is expressed without a special case.
        {QStringLiteral("quoted"), UsenetSubjectRole::Name,
         QStringLiteral(R"RX("(?<name>[^"]{1,255})")RX"),
         UsenetSubjectPick::First, false, true},

        // An unquoted token that looks like a filename: a dot followed by a
        // plausible extension. Deliberately narrow -- anything looser starts
        // matching release names with dots in them ("Some.Release.Name.2024").
        {QStringLiteral("bare-extension"), UsenetSubjectRole::Name,
         QStringLiteral(R"((?<name>[^\s"]+\.(?:part\d+\.rar|vol\d+\+\d+\.par2|par2|rar|r\d{2,3}|7z|zip|nfo|sfv|mkv|mp4|avi|iso|\d{3})))"),
         UsenetSubjectPick::Last, true, true},
    };
}

// ---------------------------------------------------------------------------
// Compiling
// ---------------------------------------------------------------------------

SubjectRuleSet SubjectRuleSet::compile(const QList<UsenetSubjectPattern>& patterns)
{
    SubjectRuleSet set;
    QList<Rule>* byRole[3] = {&set.m_part, &set.m_file, &set.m_name};

    int mentioned[3] = {0, 0, 0};   // listed at all, enabled or not
    int enabled[3]   = {0, 0, 0};
    int compiled[3]  = {0, 0, 0};

    for (const UsenetSubjectPattern& p : patterns) {
        const int r = roleIndex(p.role);
        ++mentioned[r];
        if (!p.enabled)
            continue;
        ++enabled[r];

        Rule rule;
        if (!buildRule(p, rule, /*warn=*/true))
            continue;

        byRole[r]->append(std::move(rule));
        ++compiled[r];
    }

    // A role nobody mentioned keeps the built-ins, and so does one whose every
    // enabled rule turned out to be a typo — a config that is entirely broken
    // should behave like no config at all, because that is the state a user can
    // reason about. A role whose rules were all *disabled* keeps none: that is a
    // decision rather than a mistake, and the only way to turn a role off.
    QList<UsenetSubjectRole> needDefaults;
    for (int r = 0; r < 3; ++r) {
        if (mentioned[r] == 0 || (enabled[r] > 0 && compiled[r] == 0))
            needDefaults.append(static_cast<UsenetSubjectRole>(r));
    }
    if (needDefaults.isEmpty())
        return set;

    for (const UsenetSubjectRole role : needDefaults) {
        if (mentioned[roleIndex(role)] > 0) {
            logWarning(QStringLiteral("No usable subject rule left for \"%1\"; "
                                      "falling back to the built-in ones")
                           .arg(usenetSubjectRoleName(role)));
        }
    }

    for (const UsenetSubjectPattern& p : builtInPatterns()) {
        if (!needDefaults.contains(p.role))
            continue;
        Rule rule;
        if (buildRule(p, rule, /*warn=*/false))
            byRole[roleIndex(p.role)]->append(std::move(rule));
    }
    return set;
}

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

SubjectInfo SubjectRuleSet::parse(const QString& subject) const
{
    SubjectInfo info;
    if (subject.isEmpty() || subject.size() > kMaxSubjectChars)
        return info;

    for (const Rule& rule : m_part) {
        const auto m = rule.takeLast ? lastMatch(rule.re, subject) : rule.re.match(subject);
        if (!m.hasMatch())
            continue;
        info.part  = m.captured(rule.indexGroup).toInt();
        info.total = m.captured(rule.totalGroup).toInt();
        break;
    }

    for (const Rule& rule : m_file) {
        const auto m = rule.takeLast ? lastMatch(rule.re, subject) : rule.re.match(subject);
        if (!m.hasMatch())
            continue;
        info.fileIndex = m.captured(rule.indexGroup).toInt();
        info.fileTotal = m.captured(rule.totalGroup).toInt();
        break;
    }

    // The first rule that *matches* ends the role, even if what it captured
    // trims to nothing: a quoted name wins outright, which is what the hand-
    // written version did by returning early, and a subject like
    //   Some.Release.rar " " yEnc (1/2)
    // therefore yields no filename rather than falling through to the bare-name
    // rule. Preserved deliberately; changing it is its own decision.
    for (const Rule& rule : m_name) {
        const auto m = rule.takeLast ? lastMatch(rule.re, subject) : rule.re.match(subject);
        if (!m.hasMatch())
            continue;
        info.fileName = m.captured(rule.nameGroup).trimmed();
        break;
    }

    // Nothing recognisable is normal for an obfuscated post — the caller reads
    // the real name from the first article's "=ybegin name=", or from the PAR2
    // set when that is scrambled too.
    return info;
}

// ---------------------------------------------------------------------------
// The current rules
// ---------------------------------------------------------------------------

std::shared_ptr<const SubjectRuleSet> subjectRules()
{
    // thread_local rather than a guarded singleton: this module states that
    // there is not a mutex in it, and the cost is one extra compile per thread
    // that ever parses a subject.
    thread_local quint64 cachedRevision = 0;
    thread_local std::shared_ptr<const SubjectRuleSet> cached;

    const quint64 now = Preferences::usenetSubjectPatternsRevision();
    if (!cached || cachedRevision != now) {
        cached = std::make_shared<const SubjectRuleSet>(
            SubjectRuleSet::compile(thePrefs.usenetSubjectPatterns()));
        cachedRevision = now;
    }
    return cached;
}

SubjectInfo parseSubject(const QString& subject)
{
    return subjectRules()->parse(subject);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

bool SubjectRuleSet::buildRule(const UsenetSubjectPattern& p, Rule& out, bool warn)
{
    const auto refuse = [&](const QString& why) {
        if (warn)
            logWarning(QStringLiteral("Subject rule \"%1\": %2").arg(p.name, why));
        return false;
    };

    out.name = p.name;
    out.takeLast = p.pick == UsenetSubjectPick::Last;
    out.re.setPattern(p.pattern);
    if (p.caseInsensitive)
        out.re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);

    if (!out.re.isValid()) {
        return refuse(QStringLiteral("will not compile at offset %1: %2")
                          .arg(out.re.patternErrorOffset())
                          .arg(out.re.errorString()));
    }

    // Resolve the named groups once, here rather than per subject. Positional
    // groups are refused: shift them by one — which is all it takes to wrap an
    // alternation — and the parser reports "part 97 of 3" with no error
    // anywhere, and this component has no channel to surface a wrong answer on.
    const QStringList names = out.re.namedCaptureGroups();
    out.indexGroup = int(names.indexOf(QStringLiteral("index")));
    out.totalGroup = int(names.indexOf(QStringLiteral("total")));
    out.nameGroup  = int(names.indexOf(QStringLiteral("name")));

    if (p.role == UsenetSubjectRole::Name) {
        if (out.nameGroup < 0)
            return refuse(QStringLiteral("needs a named group (?<name>...)"));
    } else if (out.indexGroup < 0 || out.totalGroup < 0) {
        // Both, never one. "Part 3 of unknown" is a state nothing downstream has
        // ever seen, and partsTotal feeds the completeness check.
        return refuse(QStringLiteral("needs named groups (?<index>...) and (?<total>...)"));
    }

    // A pattern that matches the empty string matches everywhere, is useless,
    // and under pick:last walks the whole subject to prove it.
    if (out.re.match(QString()).hasMatch())
        return refuse(QStringLiteral("matches the empty string"));

    out.re.optimize();
    return true;
}

} // namespace eMule::usenet

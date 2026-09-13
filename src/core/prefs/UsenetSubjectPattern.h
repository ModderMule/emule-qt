#pragma once

/// @file UsenetSubjectPattern.h
/// @brief One user-supplied rule for reading a yEnc subject line.
///
/// Lives in core/prefs rather than in src/usenet for the reason NewsServer does:
/// it is a stored preference first. Core holds only what the user typed — no
/// QRegularExpression, no defaults. The built-in rule set stays in
/// src/usenet/nzb/SubjectParser.cpp, so improving it in a release still reaches
/// an installation that already has a preferences.yml.
///
/// Why this is configurable at all: obfuscation schemes change faster than
/// releases ship, and a subject heuristic that needs a rebuild to follow them is
/// a heuristic that falls behind. The rules come from nZEDb's
/// `collection_regexes`, the only battle-tested corpus of these, and that project
/// keeps them in a database table for exactly this reason.

#include <QString>
#include <QtTypes>

namespace eMule {

/// Which field a rule fills. The three are evaluated independently, so a set
/// that only tunes filenames keeps the counters it did not mention.
enum class UsenetSubjectRole : quint8 {
    Part = 0,   ///< the yEnc `(n/m)` article counter
    File = 1,   ///< a leading `[n/m]` file counter
    Name = 2,   ///< the filename
};

/// Which match in the subject to take when a rule matches more than once.
///
/// Not a detail: the part counter is the *last* parenthesised pair on the line,
/// which is the whole of what keeps a "(2011)" in a title from beating a genuine
/// "(1/9)" at the end. Expressing that as data is most of the point of this file
/// — before, it was implied by which helper function a call site happened to use.
enum class UsenetSubjectPick : quint8 {
    First = 0,
    Last  = 1,
};

[[nodiscard]] inline QString usenetSubjectRoleName(UsenetSubjectRole role)
{
    switch (role) {
    case UsenetSubjectRole::Part: return QStringLiteral("part");
    case UsenetSubjectRole::File: return QStringLiteral("file");
    case UsenetSubjectRole::Name: return QStringLiteral("name");
    }
    return QStringLiteral("name");
}

/// Roles are written as words because this preference is hand-edited; a number
/// would be unreadable. An unknown word is *not* defaulted — the caller drops
/// the rule, since a rule whose role we guessed would fill the wrong field.
[[nodiscard]] inline bool usenetSubjectRoleFromName(const QString& text,
                                                    UsenetSubjectRole& out)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("part")) { out = UsenetSubjectRole::Part; return true; }
    if (t == QLatin1String("file")) { out = UsenetSubjectRole::File; return true; }
    if (t == QLatin1String("name")) { out = UsenetSubjectRole::Name; return true; }
    return false;
}

[[nodiscard]] inline QString usenetSubjectPickName(UsenetSubjectPick pick)
{
    return pick == UsenetSubjectPick::Last ? QStringLiteral("last")
                                           : QStringLiteral("first");
}

/// Unlike the role, an unrecognised pick is harmless — first is the common case
/// and the rule still fills its field — so this defaults rather than refuses.
[[nodiscard]] inline UsenetSubjectPick usenetSubjectPickFromName(const QString& text)
{
    return text.trimmed().toLower() == QLatin1String("last") ? UsenetSubjectPick::Last
                                                             : UsenetSubjectPick::First;
}

/// One rule. Order within a role is the order it is tried in, and the first rule
/// that *matches* wins — which is how "a quoted filename beats a bare one" is
/// expressed without a special case.
struct UsenetSubjectPattern {
    /// Identity: what a warning names, and what duplicate rules collapse on.
    QString name;

    UsenetSubjectRole role = UsenetSubjectRole::Name;

    /// PCRE, with named captures. Positional groups are refused on purpose: a
    /// user who wraps an alternation shifts group 1 and 2 by one, and the parser
    /// then reports "part 97 of 3" with no error anywhere — this component's
    /// contract is that returning nothing is normal, so it has no channel on
    /// which to surface a wrong answer.
    ///
    /// `part` and `file` rules must capture both `index` and `total`; a `name`
    /// rule must capture `name`.
    QString pattern;

    UsenetSubjectPick pick = UsenetSubjectPick::First;
    bool caseInsensitive = false;

    /// Off keeps the rule in the file without applying it, the way a disabled
    /// server or feed does.
    bool enabled = true;

    /// Structurally usable. Says nothing about whether the regex compiles —
    /// that is decided where the rules are compiled, and a pattern that does not
    /// is still written back, because the typo is the only record of what the
    /// user was trying to do.
    [[nodiscard]] bool isValid() const { return !name.isEmpty() && !pattern.isEmpty(); }

    [[nodiscard]] QString key() const { return name.trimmed().toLower(); }
};

} // namespace eMule

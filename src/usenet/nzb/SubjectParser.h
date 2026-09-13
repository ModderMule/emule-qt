#pragma once

/// @file SubjectParser.h
/// @brief Recovering a filename and a part counter from a yEnc subject line.
///
/// A subject looks like one of these, and there is no standard:
///
///   [1/8] - "Some.Release.r00" yEnc (03/97)
///   Some.Release.part02.rar (12/97)
///   abcdef0123456789 [04/25] - "abcdef0123456789.vol00+01.par2" yEnc (1/9)
///   [3/9] xw8Ks92m1 - (2/181)
///
/// Two rules carried over from nZEDb's `collection_regexes`, which is the only
/// battle-tested corpus of these:
///
///   1. The `(n/m)` **part** counter has **no end anchor** — subjects carry
///      trailing junk after it, and anchoring drops a large minority of posts.
///   2. "No counter" and "no filename" are **normal outcomes, not errors**.
///      nZEDb keeps `dropped.no_yenc.<group>.log` files precisely because the
///      regex is never complete, and obfuscated releases carry no readable name
///      at all — the real one only arrives in the first article's
///      `=ybegin name=`, or, for a fully obfuscated post, in the PAR2 set.
///
/// Do not tighten these into something that "validates" a subject. The heuristic
/// is allowed to return nothing; the caller is required to cope.
///
/// The rules themselves are data (`usenet.subjectPatterns` in preferences.yml),
/// because obfuscation schemes change faster than releases ship and a heuristic
/// that needs a rebuild to follow them falls behind. What is compiled in is the
/// default set, and it is what runs until somebody edits the file.

#include "prefs/UsenetSubjectPattern.h"

#include <QList>
#include <QRegularExpression>
#include <QString>

#include <memory>

namespace eMule::usenet {

struct SubjectInfo {
    QString fileName;   ///< empty when the subject carries no usable name
    int part = 0;       ///< 1-based; 0 when the subject has no (n/m) counter
    int total = 0;      ///< 0 when the subject has no (n/m) counter
    int fileIndex = 0;  ///< from a leading [n/m] file counter; 0 when absent
    int fileTotal = 0;

    friend bool operator==(const SubjectInfo&, const SubjectInfo&) = default;
};

/// Longest subject that is looked at; anything longer yields nothing.
///
/// ⚠️ This is the *only* bound available. Qt 6 exposes no PCRE2 match_limit,
/// depth_limit or offset limit, and no way to interrupt a match in progress —
/// so with user-supplied patterns, catastrophic backtracking is exponential in
/// the input length and there is nothing to catch it. A 2 KB "subject" is not a
/// subject.
///
/// It skips rather than truncates on purpose: the part counter sits at the *end*
/// of the line, so truncating would corrupt exactly the field the cap protects.
inline constexpr int kMaxSubjectChars = 2048;

/// The heuristics, compiled. Built once and used for many subjects — a
/// QRegularExpression per NZB `<file>` line is not affordable.
class SubjectRuleSet {
public:
    /// The compiled-in default rules, printed verbatim in docs/usenet-module.md.
    [[nodiscard]] static QList<UsenetSubjectPattern> builtInPatterns();

    /// Compile @p patterns.
    ///
    /// A rule that will not compile is dropped with one warning — never per
    /// subject, and never fatally. A role nobody configured, or one whose every
    /// enabled rule failed to compile, takes the built-ins; a role whose rules
    /// were all *disabled* takes none, because that is a decision rather than a
    /// typo.
    [[nodiscard]] static SubjectRuleSet compile(const QList<UsenetSubjectPattern>& patterns);

    [[nodiscard]] SubjectInfo parse(const QString& subject) const;

private:
    struct Rule {
        QRegularExpression re;
        QString name;
        int indexGroup = -1;   ///< resolved once, at compile time
        int totalGroup = -1;
        int nameGroup  = -1;
        bool takeLast  = false;
    };

    /// Compile one pattern into @p out. False when it is unusable, in which
    /// case @p warn decides whether that is worth a line in the log — a built-in
    /// that fails is a bug, not a user's typo, and nothing they can act on.
    [[nodiscard]] static bool buildRule(const UsenetSubjectPattern& p, Rule& out, bool warn);

    QList<Rule> m_part;
    QList<Rule> m_file;
    QList<Rule> m_name;
};

/// The rule set for the current preferences, rebuilt only when they change.
///
/// Per-thread, so the module keeps the "not a mutex in it" property its
/// threading design rests on. Hold the pointer for a whole document: it costs a
/// refcount, and it means one NZB is parsed by one rule set even if the
/// preference changes mid-parse.
[[nodiscard]] std::shared_ptr<const SubjectRuleSet> subjectRules();

/// Best-effort parse with the current rules. Never fails — an unparseable
/// subject yields empty fields.
[[nodiscard]] SubjectInfo parseSubject(const QString& subject);

} // namespace eMule::usenet

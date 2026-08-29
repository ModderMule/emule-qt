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
///      `=ybegin name=`.
///
/// Do not tighten these into something that "validates" a subject. The heuristic
/// is allowed to return nothing; the caller is required to cope.

#include <QString>

namespace eMule::usenet {

struct SubjectInfo {
    QString fileName;   ///< empty when the subject carries no usable name
    int part = 0;       ///< 1-based; 0 when the subject has no (n/m) counter
    int total = 0;      ///< 0 when the subject has no (n/m) counter
    int fileIndex = 0;  ///< from a leading [n/m] file counter; 0 when absent
    int fileTotal = 0;
};

/// Best-effort parse. Never fails — an unparseable subject yields empty fields.
[[nodiscard]] SubjectInfo parseSubject(const QString& subject);

} // namespace eMule::usenet

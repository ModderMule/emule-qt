#include "nzb/SubjectParser.h"

#include <QRegularExpression>

namespace eMule::usenet {

namespace {

/// The yEnc part counter. Deliberately unanchored at the end — real subjects
/// append "yEnc", byte counts and assorted junk after it, and anchoring is how
/// you silently drop a large minority of posts (nZEDb learned this the hard way
/// and keeps dropped-header logs to prove it).
///
/// Anchored at the *front* of the match only in the weak sense that the counter
/// is the last parenthesised pair on the line; taking the last match rather
/// than the first is what distinguishes "(2/181)" from a "(2011)" in a title.
const QRegularExpression& partCounter()
{
    static const QRegularExpression re(QStringLiteral(R"(\((\d{1,5})\s*/\s*(\d{1,5})\))"));
    return re;
}

/// The file counter, e.g. "[03/12]". Same shape, square brackets.
const QRegularExpression& fileCounter()
{
    static const QRegularExpression re(QStringLiteral(R"(\[(\d{1,5})\s*/\s*(\d{1,5})\])"));
    return re;
}

/// A quoted filename — by far the most reliable form when it is present.
const QRegularExpression& quotedName()
{
    static const QRegularExpression re(QStringLiteral(R"RX("([^"]{1,255})")RX"));
    return re;
}

/// An unquoted token that looks like a filename: a dot followed by a plausible
/// extension. Kept deliberately narrow, because anything looser starts matching
/// release names with dots in them ("Some.Release.Name.2024").
const QRegularExpression& bareName()
{
    static const QRegularExpression re(
        QStringLiteral(R"(([^\s"]+\.(?:part\d+\.rar|vol\d+\+\d+\.par2|par2|rar|r\d{2,3}|7z|zip|nfo|sfv|mkv|mp4|avi|iso|\d{3})))"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

/// Last match of @p re in @p text, or a null match.
QRegularExpressionMatch lastMatch(const QRegularExpression& re, const QString& text)
{
    QRegularExpressionMatch best;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        best = it.next();
    return best;
}

} // namespace

SubjectInfo parseSubject(const QString& subject)
{
    SubjectInfo info;
    if (subject.isEmpty())
        return info;

    if (const auto m = lastMatch(partCounter(), subject); m.hasMatch()) {
        info.part = m.captured(1).toInt();
        info.total = m.captured(2).toInt();
    }
    if (const auto m = fileCounter().match(subject); m.hasMatch()) {
        info.fileIndex = m.captured(1).toInt();
        info.fileTotal = m.captured(2).toInt();
    }

    // A quoted name wins outright: posters who quote mean it.
    if (const auto m = quotedName().match(subject); m.hasMatch()) {
        info.fileName = m.captured(1).trimmed();
        return info;
    }

    if (const auto m = lastMatch(bareName(), subject); m.hasMatch()) {
        info.fileName = m.captured(1).trimmed();
        return info;
    }

    // Nothing recognisable. Normal for an obfuscated post — the caller reads the
    // real name from the first article's "=ybegin name=" instead.
    return info;
}

} // namespace eMule::usenet

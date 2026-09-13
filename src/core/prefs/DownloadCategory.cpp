#include "pch.h"
/// @file DownloadCategory.cpp
/// @brief Auto-categorisation — the one rule, shared by both download queues.

#include "prefs/DownloadCategory.h"

#include <QRegularExpression>
#include <QStringView>

namespace eMule {

int matchAutoCategory(const QList<DownloadCategory>& cats, const QString& name)
{
    // Nothing to match against until the user has made a category of their own.
    if (cats.size() < 2 || name.isEmpty())
        return 0;

    // Highest index first, so the most recently added category wins a tie —
    // MFC counts down for the same reason (srchybrid/DownloadQueue.cpp:1246).
    for (int i = static_cast<int>(cats.size()) - 1; i > 0; --i) {
        const QString pattern = cats.at(i).autocat.trimmed();
        if (pattern.isEmpty())
            continue;

        bool matched = false;
        if (cats.at(i).autocatIsRegexp) {
            const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
            matched = re.isValid() && re.match(name).hasMatch();
        } else {
            // '|'-separated terms; a term containing * or ? is a wildcard, any
            // other is a plain substring. MFC's loop here is written
            // `if (!cmpExt.IsEmpty()) break;` — inverted, so it bails on the
            // first real term and the non-regexp branch never matches anything
            // (its bFound also survives across outer iterations). Ported as
            // intended rather than as written; the divergence is deliberate and
            // is recorded in docs/categories.local.md.
            for (const QStringView termView : QStringView{pattern}.split(u'|', Qt::SkipEmptyParts)) {
                const QString term = termView.trimmed().toString();
                if (term.isEmpty())
                    continue;

                if (term.contains(u'*') || term.contains(u'?')) {
                    const auto wildcard = QRegularExpression::fromWildcard(
                        term, Qt::CaseInsensitive, QRegularExpression::UnanchoredWildcardConversion);
                    matched = wildcard.match(name).hasMatch();
                } else {
                    matched = name.contains(term, Qt::CaseInsensitive);
                }

                if (matched)
                    break;
            }
        }

        if (matched)
            return i;
    }

    return 0;
}

int remapCategoryIndex(int category, const QHash<uint32, uint32>& oldToNew)
{
    if (category <= 0)
        return 0;   // "All" is never remapped: index 0 always exists
    return static_cast<int>(oldToNew.value(static_cast<uint32>(category), 0));
}

} // namespace eMule

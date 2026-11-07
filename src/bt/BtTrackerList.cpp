/// @file BtTrackerList.cpp
/// @brief Tracker list edits without libtorrent — implementation.

#include "BtTrackerList.h"

#include <QSet>

#include <algorithm>

namespace eMule::bt {

namespace {

QStringList cleaned(const QStringList& urls)
{
    QStringList out;
    for (const QString& raw : urls)
        if (const QString url = raw.trimmed(); !url.isEmpty() && !out.contains(url))
            out << url;
    return out;
}

} // namespace

QList<BtTrackerEntry> editTrackerList(const QList<BtTrackerEntry>& current, const BtTrackerEdit& edit)
{
    const QStringList add = cleaned(edit.add);
    const QStringList dropList = edit.exact ? QStringList{} : cleaned(edit.remove);
    const QSet<QString> drop(dropList.cbegin(), dropList.cend());
    const QSet<QString> wanted(add.cbegin(), add.cend());

    QList<BtTrackerEntry> out;
    QSet<QString> known;
    int lastTier = 0;
    for (const BtTrackerEntry& e : current) {
        if (known.contains(e.url) || (edit.exact ? !wanted.contains(e.url) : drop.contains(e.url)))
            continue;
        out.append(e);
        known.insert(e.url);
        lastTier = std::max(lastTier, e.tier);
    }
    for (const QString& url : add) {
        if (known.contains(url))
            continue;
        out.append({url, lastTier});
        known.insert(url);
    }
    return out;
}

} // namespace eMule::bt

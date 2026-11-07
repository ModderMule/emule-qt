#pragma once

/// @file BtTrackerList.h
/// @brief Tracker list edits without libtorrent: one edit for many torrents (#143).
///
/// BtSession applies the result with replace_trackers(); kept entries keep their
/// tier, new ones go to the last tier, as Add Trackers did.

#include <QList>
#include <QString>
#include <QStringList>

namespace eMule::bt {

struct BtTrackerEntry {
    QString url;
    int tier = 0;

    bool operator==(const BtTrackerEntry&) const = default;
};

/// One tracker edit. @p exact: the list becomes @p add (in that order), @p remove ignored.
struct BtTrackerEdit {
    QStringList add;
    QStringList remove;
    bool exact = false;
};

/// @p current with @p edit applied. URLs trimmed, empty and duplicate ones dropped.
[[nodiscard]] QList<BtTrackerEntry> editTrackerList(const QList<BtTrackerEntry>& current,
                                                    const BtTrackerEdit& edit);

} // namespace eMule::bt

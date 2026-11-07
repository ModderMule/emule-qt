#pragma once

/// @file TorrentOptionsDialog.h
/// @brief Options… of the Torrents list: speed, seeding and peer limits for all
///        selected torrents at once (#143). Settings they differ in show "Varies"
///        and stay per torrent unless changed.

#include "dialogs/RememberedSizeDialog.h"

#include <QCborMap>
#include <QList>

namespace eMule {

class TorrentOptionsForm;

class TorrentOptionsDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    /// @p options: GetTorrentOptionsMany maps, one per selected torrent.
    explicit TorrentOptionsDialog(const QList<QCborMap>& options, QWidget* parent = nullptr);

    /// Only the changed settings, for SetTorrentOptionsMany.
    [[nodiscard]] QCborMap changedOptions() const;
    [[nodiscard]] TorrentOptionsForm* form() const { return m_form; }

private:
    TorrentOptionsForm* m_form = nullptr;
};

} // namespace eMule

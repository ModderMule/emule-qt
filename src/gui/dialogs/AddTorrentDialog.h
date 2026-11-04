#pragma once

/// @file AddTorrentDialog.h
/// @brief Add BitTorrent magnet links (one per line) or picked .torrent files.
///
/// Both modes share the PasteTextDialog shape plus a "Save to" row, "Start
/// paused", "Stop after metadata" and "I2P only". Files travel as their bytes, never their path: the daemon may run
/// on another machine (same rule as UsenetPanel::addNzbFile).

#include "dialogs/PasteTextDialog.h"

#include <QByteArray>
#include <QHash>

namespace eMule {

class IpcClient;

class AddTorrentDialog : public PasteTextDialog {
    Q_OBJECT

public:
    enum class Mode { Magnets, Files };

    /// @p items: magnets to pre-fill (Magnets) or the picked paths (Files).
    AddTorrentDialog(IpcClient* ipc, Mode mode, const QStringList& items = {},
                     QWidget* parent = nullptr);

    /// Cap per paste, as AddNzbUrlDialog has.
    static constexpr int kMaxItems = 100;
    /// Chrome::optionLabels indexes
    static constexpr int kStopOption = 0;
    static constexpr int kI2pOption = 1;

protected:
    void onAccepted() override;

private:
    void sendAll(const QStringList& keys, const QHash<QString, QByteArray>& fileData);

    IpcClient* m_ipc = nullptr;
    Mode m_mode;
    QStringList m_paths;
};

} // namespace eMule

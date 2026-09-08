#pragma once

/// @file AddNzbUrlDialog.h
/// @brief Queue one or more .nzb files named by http(s) URL.
///
/// The daemon does the downloading — see IpcProtocol.h, AddNzbUrl — so this
/// sends a request per line and reports which lines failed. A URL that a
/// laptop cannot reach may still be reachable from the daemon's own network,
/// which is why the GUI does not fetch anything itself.

#include "dialogs/PasteTextDialog.h"

namespace eMule {

class IpcClient;

class AddNzbUrlDialog : public PasteTextDialog {
    Q_OBJECT

public:
    explicit AddNzbUrlDialog(IpcClient* ipc, QWidget* parent = nullptr);

    /// Most links people paste are one at a time; the cap is here so a stray
    /// paste of a whole page gets a sentence instead of most of its lines
    /// bouncing off the daemon's own in-flight limit.
    static constexpr int kMaxUrls = 20;

protected:
    void onAccepted() override;

private:
    IpcClient* m_ipc;
};

} // namespace eMule

#pragma once

/// @file PasteLinksDialog.h
/// @brief Dialog for pasting eD2K links to download, matching MFC CDirectDownloadDlg.

#include "dialogs/PasteTextDialog.h"

namespace eMule {

class IpcClient;

class PasteLinksDialog : public PasteTextDialog {
    Q_OBJECT

public:
    explicit PasteLinksDialog(IpcClient* ipc, QWidget* parent = nullptr);

protected:
    void onAccepted() override;

private:
    IpcClient* m_ipc;
};

} // namespace eMule

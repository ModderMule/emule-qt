#pragma once

/// @file PasteLinksDialog.h
/// @brief Dialog for pasting eD2K links to download, matching MFC CDirectDownloadDlg.

#include "dialogs/PasteTextDialog.h"

namespace eMule {

class IpcClient;

class PasteLinksDialog : public PasteTextDialog {
    Q_OBJECT

public:
    /// @param categories the download categories, index 0 ("All") first; a category
    ///                   box is offered when there is more than that one
    ///                   (MFC CDirectDownloadDlg hides its tabs without categories)
    explicit PasteLinksDialog(IpcClient* ipc, const QStringList& categories = {},
                              QWidget* parent = nullptr);

protected:
    void onAccepted() override;

private:
    IpcClient* m_ipc;
};

} // namespace eMule

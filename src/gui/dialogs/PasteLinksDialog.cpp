#include "pch.h"
#include "dialogs/PasteLinksDialog.h"

#include "app/IpcClient.h"
#include "utils/Ed2kLinkImporter.h"

#include <QMessageBox>
#include <QPointer>

namespace eMule {

PasteLinksDialog::PasteLinksDialog(IpcClient* ipc, QWidget* parent)
    : PasteTextDialog(
          Chrome{tr("Paste eD2K Links"),
                 QStringLiteral(":/icons/eD2kLinkPaste.ico"),
                 tr("eD2K Links:"),
                 tr("Paste one or more ed2k:// links here, one per line..."),
                 tr("Download")},
          parent)
    , m_ipc(ipc)
{
}

void PasteLinksDialog::onAccepted()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Not Connected"),
            tr("Not connected to the daemon."));
        return;
    }

    // Manual: this dialog is the confirmation, so no second prompt. Only files that are
    // already downloading or shared are dropped — pasting the link of a completed or
    // cancelled file is how you deliberately re-download it.
    const QPointer<PasteLinksDialog> self(this);
    Ed2kLinkImporter::importLinks(
        text(), m_ipc, this,
        Ed2kLinkImporter::Source::Manual,
        Ed2kLinkImporter::Prompt::Silent,
        [self](const Ed2kLinkImporter::Result& result) {
            if (!self)
                return;

            if (!result.invalid.isEmpty()) {
                QMessageBox::warning(self, tr("Invalid Links"),
                    tr("The following links could not be parsed:\n\n%1")
                        .arg(result.invalid.join(QLatin1Char('\n'))));
            }

            // Skipped links report themselves — the importer logs each one and, for a single
            // pasted link, shows the "you already have it" box. A summary here on top of that
            // would make this dialog behave differently from the Transfers context menu.
            // A configuration link closes this too: it starts no download, but the
            // dialog has done its job and leaving it open behind the confirmation
            // box reads as though the paste failed.
            if (result.added > 0 || result.httpCacheConfigs > 0)
                self->accept();
        });
}

} // namespace eMule

#include "pch.h"
#include "dialogs/PasteLinksDialog.h"

#include "app/IpcClient.h"
#include "utils/Ed2kLinkImporter.h"

#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

PasteLinksDialog::PasteLinksDialog(IpcClient* ipc, QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
{
    setWindowTitle(tr("Paste eD2K Links"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/eD2kLinkPaste.ico")));
    resize(450, 250);

    auto* layout = new QVBoxLayout(this);

    auto* label = new QLabel(tr("eD2K Links:"), this);
    layout->addWidget(label);

    m_edit = new QPlainTextEdit(this);
    m_edit->setPlaceholderText(tr("Paste one or more ed2k:// links here, one per line..."));
    layout->addWidget(m_edit);

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    m_downloadBtn = new QPushButton(tr("Download"), this);
    m_downloadBtn->setDefault(true);
    m_downloadBtn->setEnabled(false);
    btnLayout->addWidget(m_downloadBtn);

    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addWidget(cancelBtn);

    layout->addLayout(btnLayout);

    connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
        m_downloadBtn->setEnabled(!m_edit->toPlainText().trimmed().isEmpty());
    });
    connect(m_downloadBtn, &QPushButton::clicked, this, &PasteLinksDialog::onDownload);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void PasteLinksDialog::onDownload()
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
        m_edit->toPlainText().trimmed(), m_ipc, this,
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

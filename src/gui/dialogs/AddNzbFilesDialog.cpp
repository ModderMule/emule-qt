#include "pch.h"
#include "dialogs/AddNzbFilesDialog.h"

#include "app/IpcClient.h"
#include "dialogs/NzbFileChooserDialog.h"

#include <QFileInfo>
#include <QPushButton>

namespace eMule {

AddNzbFilesDialog::AddNzbFilesDialog(IpcClient* ipc, const QStringList& paths,
                                     const QStringList& categories, QWidget* parent)
    : PasteTextDialog(
          Chrome{tr("Add NZB"),
                 QStringLiteral(":/icons/Usenet.ico"),
                 tr("NZB files:"),
                 QString(),
                 tr("Add"),
                 QSize(470, 260),
                 tr("Password:"),
                 /*readOnlyText*/ true,
                 categories},
          parent)
    , m_ipc(ipc)
    , m_paths(paths)
{
    // File names, not full paths: the user just picked these in a file dialog
    // and knows where they came from, and a column of home-directory prefixes
    // pushes the part that identifies the release off the right edge.
    QStringList names;
    names.reserve(paths.size());
    for (const QString& path : paths)
        names.append(QFileInfo(path).fileName());
    setLines(names);

    // The daemon reads the NZBs, so this needs a connection. Without one the
    // releases queue whole, which is what adding did before.
    m_chooseButton = addActionButton(tr("Choose Files…"));
    m_chooseButton->setEnabled(m_ipc && m_ipc->isConnected());
    connect(m_chooseButton, &QPushButton::clicked, this, &AddNzbFilesDialog::chooseFiles);
}

void AddNzbFilesDialog::onAccepted()
{
    // Read before accept(): the base disables the field on the way out, and the
    // caller asks for this after exec() has returned.
    m_password = password();
    m_category = queueCategory();
    m_priority = queuePriority();
    m_paused = queuePaused();
    accept();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void AddNzbFilesDialog::chooseFiles()
{
    NzbFileChooserDialog chooser(m_ipc, m_paths, m_skipped, this);
    if (chooser.exec() != QDialog::Accepted)
        return;

    m_skipped = chooser.skippedFiles();
    int count = 0;
    for (const QList<int>& files : std::as_const(m_skipped))
        count += int(files.size());
    m_chooseButton->setText(count > 0 ? tr("Choose Files… (%n skipped)", nullptr, count)
                                      : tr("Choose Files…"));
}

} // namespace eMule

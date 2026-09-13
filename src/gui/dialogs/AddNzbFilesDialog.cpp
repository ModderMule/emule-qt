#include "pch.h"
#include "dialogs/AddNzbFilesDialog.h"

#include <QFileInfo>

namespace eMule {

AddNzbFilesDialog::AddNzbFilesDialog(const QStringList& paths,
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
{
    // File names, not full paths: the user just picked these in a file dialog
    // and knows where they came from, and a column of home-directory prefixes
    // pushes the part that identifies the release off the right edge.
    QStringList names;
    names.reserve(paths.size());
    for (const QString& path : paths)
        names.append(QFileInfo(path).fileName());
    setLines(names);
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

} // namespace eMule

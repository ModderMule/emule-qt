#include "pch.h"
/// @file RememberedSizeDialog.cpp
/// @brief Dialog that remembers its last size — see RememberedSizeDialog.h.

#include "dialogs/RememberedSizeDialog.h"

#include "app/UiState.h"
#include "utils/DialogSizing.h"

namespace eMule {

RememberedSizeDialog::RememberedSizeDialog(QWidget* parent, Qt::WindowFlags flags)
    : QDialog(parent, flags)
{
}

void RememberedSizeDialog::setVisible(bool visible)
{
    // Before QDialog::setVisible(): it centres the window on its parent with the size
    // it has at that moment, so resizing afterwards would leave it off-centre
    if (visible && !isVisible() && !m_restored) {
        m_restored = true;
        restoreSize();
    }
    // Every close path -- OK, Cancel, Esc, the close button, done() -- ends in a hide
    if (!visible && isVisible())
        storeSize();

    QDialog::setVisible(visible);
}

QString RememberedSizeDialog::sizeKey() const
{
    QString name = QString::fromLatin1(metaObject()->className());
    if (const qsizetype sep = name.lastIndexOf(QLatin1String("::")); sep >= 0)
        name = name.mid(sep + 2);
    return name;
}

// ── private ────────────────────────────────────────────────────────────

void RememberedSizeDialog::restoreSize()
{
    if (minimumSize() == maximumSize())
        return;   // fixed size, nothing the user could have changed

    const QSize stored = theUiState.dialogSize(sizeKey());
    if (!stored.isValid())
        return;

    // Cut like DialogSizing cuts the default: a size left on a big monitor must not
    // follow the user to a small one
    resize(stored.expandedTo(minimumSize())
               .boundedTo(maximumSize())
               .boundedTo(DialogSizing::screenBudget(this)));
}

void RememberedSizeDialog::storeSize() const
{
    if (minimumSize() == maximumSize() || isMaximized() || isFullScreen() || isMinimized())
        return;
    theUiState.setDialogSize(sizeKey(), size());
}

} // namespace eMule

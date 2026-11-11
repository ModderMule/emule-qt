#pragma once

/// @file ModalHost.h
/// @brief Keeps pop-ups the user did not open usable while an app-modal dialog runs.
///
/// Options (and other exec() dialogs) block every window but their own children, so a
/// puzzle or a question from the core parented to the main window can't be clicked.
/// adopt() parents such a dialog to the top modal one and moves it back to @p home once
/// that closes. Header-only so the tests that compile single panel sources need no extra .cpp.

#include <QApplication>
#include <QDialog>
#include <QPointer>
#include <QWidget>

namespace eMule::ModalHost {

/// Hosts @p dlg on the top app-modal dialog, if one is open, else leaves it on its parent.
/// Call before show(); a hosted dialog is non-modal so the host stays usable too.
/// @p home takes it back when the host closes (none: its parent at the time of the call).
/// @return true when hosted
inline bool adopt(QWidget* dlg, QWidget* home = nullptr)
{
    if (!dlg)
        return false;
    if (!home)
        home = dlg->parentWidget();
    auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    if (!modal || modal == dlg || dlg->isAncestorOf(modal) || modal->isAncestorOf(dlg))
        return false;
    const bool visible = dlg->isVisible();
    const QPoint pos = dlg->pos();
    dlg->setParent(modal, dlg->windowFlags() | Qt::Dialog);
    // window-modal (open()) over the host would block the host itself: a sheet on macOS
    if (dlg->windowModality() == Qt::WindowModal)
        dlg->setWindowModality(Qt::NonModal);
    if (visible) {
        dlg->move(pos);
        dlg->show();
    }
    // finished comes before the host dies, and its children with it
    QObject::connect(modal, &QDialog::finished, dlg, [dlg, home = QPointer<QWidget>(home)] {
        const bool shown = !dlg->isHidden();   // closing the host only hides it implicitly
        const QPoint at = dlg->pos();
        dlg->setParent(home, dlg->windowFlags() | Qt::Dialog);
        dlg->move(at);
        if (shown)
            dlg->show();
        adopt(dlg, home);   // a nested modal still open
    }, Qt::SingleShotConnection);
    return true;
}

/// show() for a pop-up the user did not open: hosted, raised and active.
inline void show(QWidget* dlg, QWidget* home = nullptr)
{
    adopt(dlg, home);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

/// open() for a pop-up the user did not open: window-modal unless hosted, where it
/// would block the host (Options) too.
inline void open(QDialog* dlg, QWidget* home = nullptr)
{
    if (adopt(dlg, home))
        show(dlg);
    else
        dlg->open();
}

} // namespace eMule::ModalHost

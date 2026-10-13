#pragma once

/// @file AsyncMessageBox.h
/// @brief Message boxes that do not block — the only kind an IPC reply may open.
///
/// QMessageBox::warning() and friends run a nested event loop. Inside an IpcClient
/// callback that loop sits on top of the socket read: IpcConnection dispatches
/// nothing more until the box is closed, so every list in the window stops
/// updating — and with the box hidden behind another window, or the main window in
/// the tray, the GUI looks hung for good. These return at once instead.
///
/// Header-only on purpose: GUI tests list their sources by hand, and a helper with
/// a .cpp would have to be added to every one of them.

#include <QMessageBox>
#include <QString>

namespace eMule::AsyncMessageBox {

/// Show a window-modal box with an OK button and return immediately.
inline void show(QMessageBox::Icon icon, QWidget* parent, const QString& title,
                 const QString& text)
{
    auto* box = new QMessageBox(icon, title, text, QMessageBox::Ok, parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

inline void warning(QWidget* parent, const QString& title, const QString& text)
{
    show(QMessageBox::Warning, parent, title, text);
}

inline void information(QWidget* parent, const QString& title, const QString& text)
{
    show(QMessageBox::Information, parent, title, text);
}

} // namespace eMule::AsyncMessageBox

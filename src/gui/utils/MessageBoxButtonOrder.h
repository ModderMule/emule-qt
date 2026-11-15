#pragma once

/// @file MessageBoxButtonOrder.h
/// @brief One Yes/No (OK/Cancel) order in every message box, the platform's own.

#include <QEvent>
#include <QMessageBox>
#include <QObject>

namespace eMule {

/// App-wide filter: every QMessageBox is drawn by Qt, whose QDialogButtonBox lays
/// buttons out per platform style guide (macOS/GNOME: affirmative trailing,
/// "No  Yes"; Windows/KDE: "Yes  No").
/// Why: on macOS a static QMessageBox call becomes a native NSAlert, and AppKit
/// moves the Return-key button to the trailing side. With default No (most
/// confirmations) that read "Yes  No", while boxes Qt draws itself (custom
/// buttons, rich text, window-modal on Tahoe) read "No  Yes". Set at Polish:
/// the native decision comes after it, Show is too late.
class MessageBoxButtonOrder : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Polish)
            if (auto* box = qobject_cast<QMessageBox*>(watched))
                box->setOption(QMessageBox::Option::DontUseNativeDialog);
        return QObject::eventFilter(watched, event);
    }
};

} // namespace eMule

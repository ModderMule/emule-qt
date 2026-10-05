#pragma once

/// @file UrlPrefField.h
/// @brief A URL input bound to a daemon-owned preference that is never empty.
///
/// Shared by the Kad panel (nodes.dat) and the Servers panel (server.met): an
/// emptied field falls back to the shipped default, a changed one goes to the daemon.

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "prefs/Preferences.h"

#include <QLineEdit>
#include <QString>

#include <functional>
#include <utility>

namespace eMule {

/// Show @p url from its start — a long URL otherwise shows its tail.
inline void showUrlPref(QLineEdit* edit, const QString& url)
{
    edit->setText(url);
    edit->setCursorPosition(0);
}

/// Reload the field from the preference, unless the user is typing in it.
inline void refreshUrlPrefField(QLineEdit* edit, QString (Preferences::*getter)() const)
{
    if (!edit->hasFocus())
        showUrlPref(edit, (thePrefs.*getter)());
}

/// The field's URL, or @p fallback while it is empty. Enter reaches a click
/// handler before editingFinished has put the default back.
[[nodiscard]] inline QString urlPrefFieldValue(const QLineEdit* edit, QLatin1StringView fallback)
{
    const QString url = edit->text().trimmed();
    return url.isEmpty() ? QString(fallback) : url;
}

/// Fill @p edit from the preference and keep the two in step: on focus loss or
/// Enter an empty field reverts to @p fallback, and a changed URL is sent as a
/// single-key SetPreferences. @p ipc is asked late — panels get their client
/// after construction.
inline void bindUrlPrefField(QLineEdit* edit, const QString& key, QLatin1StringView fallback,
                             QString (Preferences::*getter)() const,
                             void (Preferences::*setter)(const QString&),
                             std::function<IpcClient*()> ipc)
{
    showUrlPref(edit, (thePrefs.*getter)());

    QObject::connect(edit, &QLineEdit::editingFinished, edit,
                     [edit, key, fallback, getter, setter, ipc = std::move(ipc)] {
        const QString url = urlPrefFieldValue(edit, fallback);
        if (url != edit->text())
            showUrlPref(edit, url);
        if (url == (thePrefs.*getter)())
            return;

        (thePrefs.*setter)(url);
        IpcClient* client = ipc();
        if (!client || !client->isConnected())
            return;
        Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
        req.append(key);
        req.append(url);
        client->sendRequest(std::move(req));
    });
}

} // namespace eMule

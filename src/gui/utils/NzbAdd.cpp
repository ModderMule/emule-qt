#include "pch.h"
/// @file NzbAdd.cpp
/// @brief The one NZB add exchange, shared by every GUI intake path.

#include "utils/NzbAdd.h"

#include "app/IpcClient.h"

#include <QApplication>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>

#include <utility>

namespace eMule::gui {

namespace {

/// The refusal sentence, with a fallback: an empty error reaches the user as an
/// empty dialog, which reads as the add having silently worked.
[[nodiscard]] QString refusalText(const Ipc::IpcMessage& resp, const QString& title)
{
    const QString reason = resp.fieldString(1);
    return reason.isEmpty() ? QObject::tr("Could not add \"%1\".").arg(title) : reason;
}

/// A box one event-loop turn from now. Never inside the stack that delivered an
/// IPC reply — see the header.
void warnLater(const QPointer<QWidget>& parent, const QString& text)
{
    QTimer::singleShot(0, qApp, [parent, text] {
        QMessageBox::warning(parent, QObject::tr("Add NZB"), text);
    });
}

} // namespace

void sendNzbAdd(IpcClient* ipc, QWidget* parent, const QString& title,
                std::function<Ipc::IpcMessage(bool force)> build,
                std::function<void(bool added)> done)
{
    if (!ipc || !ipc->isConnected()) {
        if (done)
            done(false);
        return;
    }

    const QPointer<QWidget> safeParent(parent);
    const QPointer<IpcClient> safeIpc(ipc);

    ipc->sendRequest(build(false),
        [safeIpc, safeParent, title, build = std::move(build), done = std::move(done)]
        (const Ipc::IpcMessage& resp) mutable
    {
        if (resp.fieldBool(0)) {
            if (done)
                done(true);
            return;
        }

        const auto outcome = static_cast<NzbAddOutcome>(resp.fieldInt(2));
        const QString reason = refusalText(resp, title);

        // One turn of the event loop puts every dialog below outside the stack
        // that delivered this reply.
        QTimer::singleShot(0, qApp, [safeIpc, safeParent, title, reason, outcome,
                                     build = std::move(build), done = std::move(done)]() mutable {
            if (outcome != NzbAddOutcome::AlreadyDownloaded) {
                QMessageBox::warning(safeParent, QObject::tr("Add NZB"), reason);
                if (done)
                    done(false);
                return;
            }

            // Not a refusal: the release has left the queue, and wanting it again
            // is a thing people legitimately do. A feed never reaches here — the
            // daemon gives it the same verdict as a terminal answer.
            const bool again =
                QMessageBox::question(safeParent, QObject::tr("Add NZB"),
                                      QObject::tr("%1\n\nDownload it again?").arg(reason),
                                      QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
            if (!again || !safeIpc || !safeIpc->isConnected()) {
                if (done)
                    done(false);
                return;
            }

            // Same request, one bit different. Nothing was parked on the daemon
            // between the question and this answer.
            safeIpc->sendRequest(build(true),
                [safeParent, title, done = std::move(done)](const Ipc::IpcMessage& retry) {
                if (retry.fieldBool(0)) {
                    if (done)
                        done(true);
                    return;
                }
                warnLater(safeParent, refusalText(retry, title));
                if (done)
                    done(false);
            });
        });
    });
}

} // namespace eMule::gui

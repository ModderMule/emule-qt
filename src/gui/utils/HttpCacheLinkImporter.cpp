#include "pch.h"
#include "utils/HttpCacheLinkImporter.h"

#include "app/IpcClient.h"

#include "IpcMessage.h"
#include "protocol/ED2KLink.h"
#include "utils/Log.h"
#include "utils/StatusBarNotifier.h"

#include <QApplication>
#include <QCborMap>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// What the server said about itself, as far as the dialog cares.
struct ProbeAnswer {
    bool ok = false;
    QString error;
    int version = 0;
    QString implementation;
    bool uploadRequiresAuth = true;
    QString currentBaseUrl;   ///< what is configured now, "" if nothing
    bool unchanged = false;   ///< this link would change nothing
};

ProbeAnswer readProbe(const IpcMessage& resp)
{
    ProbeAnswer answer;
    if (!resp.fieldBool(0)) {
        answer.error = HttpCacheLinkImporter::tr("The core did not answer.");
        return answer;
    }

    const QCborMap map = resp.fieldMap(1);
    answer.ok                 = map.value(QStringLiteral("ok")).toBool();
    answer.error              = map.value(QStringLiteral("error")).toString();
    answer.version            = static_cast<int>(map.value(QStringLiteral("version")).toInteger());
    answer.implementation     = map.value(QStringLiteral("implementation")).toString();
    answer.uploadRequiresAuth = map.value(QStringLiteral("uploadRequiresAuth")).toBool(true);
    answer.currentBaseUrl     = map.value(QStringLiteral("currentBaseUrl")).toString();
    answer.unchanged          = map.value(QStringLiteral("unchanged")).toBool();
    return answer;
}

/// The confirmation. Shows the host, never the secret.
bool confirm(const ED2KHttpCacheLink& link, const ProbeAnswer& answer, QWidget* parent)
{
    const QUrl url(link.baseUrl);
    const bool plain = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0;
    const QString label = link.name.isEmpty() ? url.host() : link.name;

    QStringList details;
    details << HttpCacheLinkImporter::tr("Server: %1").arg(url.host());
    if (!link.keyId.isEmpty())
        details << HttpCacheLinkImporter::tr("Key: %1").arg(link.keyId);
    details << HttpCacheLinkImporter::tr("Version %1%2")
                   .arg(answer.version)
                   .arg(answer.implementation.isEmpty()
                            ? QString()
                            : QStringLiteral(" (%1)").arg(answer.implementation));
    if (!answer.uploadRequiresAuth)
        details << HttpCacheLinkImporter::tr("This server also accepts uploads without a key.");

    // Replacing an existing configuration is the one outcome somebody may not
    // have meant, so it is named rather than merely implied.
    if (!answer.currentBaseUrl.isEmpty() && answer.currentBaseUrl != link.baseUrl)
        details << HttpCacheLinkImporter::tr("This replaces the current server, %1.")
                       .arg(QUrl(answer.currentBaseUrl).host());

    QString warning;
    if (plain) {
        // Not a formality: over http the key travels in the clear on every
        // request, and so does every chunk URL — and a chunk URL is the only
        // thing keeping that chunk's ciphertext from anyone who asks for it.
        warning = HttpCacheLinkImporter::tr(
            "\n\nThis link uses plain HTTP. The key and every chunk address will "
            "cross the network unencrypted.");
    }

    QMessageBox box(parent);
    box.setIcon(plain ? QMessageBox::Warning : QMessageBox::Question);
    box.setWindowTitle(HttpCacheLinkImporter::tr("HTTP Cache"));
    box.setText(HttpCacheLinkImporter::tr("Use \"%1\" as your HTTP Cache server?").arg(label));
    box.setInformativeText(details.join(QLatin1Char('\n'))
                           + HttpCacheLinkImporter::tr(
                                 "\n\nHTTP Cache will be enabled and this key stored "
                                 "for uploads.")
                           + warning);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    return box.exec() == QMessageBox::Yes;
}

/// Show a warning box, but never from inside the call stack that delivered the
/// IPC reply.
///
/// A modal box spins a nested event loop, and a quit arriving during it — a
/// signal, Cmd-Q, a logout — unwinds *every* loop at once. main() then destroys
/// the IpcClient and its socket while the socket-read notification that opened
/// the box is still below us on the stack, and Qt returns into freed memory. One
/// turn of the event loop puts the dialog outside that stack entirely.
void warnLater(const QPointer<QWidget>& parent, const QString& text)
{
    QTimer::singleShot(0, qApp, [parent, text] {
        // parent may have died with the dialog that started this; a parentless box
        // is still shown, because a refusal the user never sees is
        // indistinguishable from the link having worked.
        QMessageBox::warning(parent, HttpCacheLinkImporter::tr("HTTP Cache"), text);
    });
}

/// Everything after the handshake: report, or ask and store. Runs one event-loop
/// turn after the reply, for the reason warnLater() gives.
void finishProbe(const ED2KHttpCacheLink& link, const ProbeAnswer& answer, IpcClient* ipc,
                 const QPointer<QWidget>& parent, const std::function<void(bool)>& done,
                 const std::function<void()>& beforePrompt)
{
    if (!ipc || !ipc->isConnected())
        return;

    const QString host = QUrl(link.baseUrl).host();

    if (!answer.ok) {
        logError(HttpCacheLinkImporter::tr("HTTP Cache link refused: %1").arg(answer.error));
        StatusBarNotifier::post(HttpCacheLinkImporter::tr("HTTP Cache link refused"));
        if (beforePrompt)
            beforePrompt();
        warnLater(parent, answer.error);
        if (done)
            done(false);
        return;
    }

    // Already exactly what is configured. Saying so beats a dialog that changes
    // nothing — the clipboard watcher would otherwise raise one every time this
    // link passes through the clipboard.
    if (answer.unchanged) {
        logInfo(HttpCacheLinkImporter::tr("HTTP Cache is already configured for %1.").arg(host));
        StatusBarNotifier::post(
            HttpCacheLinkImporter::tr("HTTP Cache is already configured for %1.").arg(host));
        if (done)
            done(false);
        return;
    }

    if (beforePrompt)
        beforePrompt();

    if (!confirm(link, answer, parent)) {
        logInfo(HttpCacheLinkImporter::tr("HTTP Cache configuration for %1 was not applied.")
                    .arg(host));
        if (done)
            done(false);
        return;
    }

    IpcMessage apply(IpcMsgType::ApplyHttpCacheConfig);
    apply.append(link.baseUrl);
    apply.append(link.secret);

    ipc->sendRequest(std::move(apply), [host, parent, done](const IpcMessage& resp) {
        const bool ok = resp.fieldBool(0);
        if (ok) {
            logInfo(HttpCacheLinkImporter::tr("HTTP Cache configured for %1.").arg(host));
            StatusBarNotifier::post(
                HttpCacheLinkImporter::tr("HTTP Cache configured for %1.").arg(host));
        } else {
            const QString error = resp.fieldString(1);
            logError(HttpCacheLinkImporter::tr("HTTP Cache configuration failed: %1").arg(error));
            warnLater(parent, error);
        }
        if (done)
            done(ok);
    });
}

} // anonymous namespace

void HttpCacheLinkImporter::apply(const ED2KHttpCacheLink& link, IpcClient* ipc, QWidget* parent,
                                  std::function<void(bool)> done,
                                  std::function<void()> beforePrompt)
{
    if (!ipc || !ipc->isConnected()) {
        if (done)
            done(false);
        return;
    }

    // The daemon probes, not the GUI: it may be on another machine, and the only
    // reachability that matters is that of the node which will use the cache.
    IpcMessage msg(IpcMsgType::ProbeHttpCacheServer);
    msg.append(link.baseUrl);
    msg.append(link.secret);

    const QPointer<QWidget> safeParent(parent);

    ipc->sendRequest(std::move(msg),
        [link, ipc, safeParent, done = std::move(done), beforePrompt = std::move(beforePrompt)]
        (const IpcMessage& resp)
    {
        if (!ipc->isConnected())
            return;

        const ProbeAnswer answer = readProbe(resp);
        QTimer::singleShot(0, qApp, [link, answer, ipc, safeParent, done, beforePrompt] {
            finishProbe(link, answer, ipc, safeParent, done, beforePrompt);
        });
    });
}

} // namespace eMule

#include "pch.h"
#include "utils/Ed2kLinkImporter.h"

#include "app/IpcClient.h"

#include "IpcMessage.h"
#include "protocol/ED2KLink.h"
#include "utils/HttpCacheLinkImporter.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/StatusBarNotifier.h"

#include <QApplication>
#include <QCborArray>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>

#include <utility>
#include <vector>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// One parsed file link, kept together with the line it came from. The raw line is what
/// gets sent to the daemon: it carries the hashset, AICH hash and source hints that a
/// rebuilt link would lose.
struct ParsedLink {
    QString rawLine;
    QString name;
    QString hashHex;  ///< uppercase, as produced by md4str — the daemon's own format
    uint64 size = 0;
};

/// Split link text into file links, collecting HTTP Cache configuration links into
/// @p configs and unparseable candidates into @p invalid.
std::vector<ParsedLink> parseLines(const QString& text, QStringList& invalid,
                                   std::vector<ED2KHttpCacheLink>& configs)
{
    std::vector<ParsedLink> links;

    for (const QString& candidate : Ed2kLinkImporter::splitLinks(text)) {
        auto parsed = parseED2KLink(candidate);

        if (const auto* cfg = parsed ? std::get_if<ED2KHttpCacheLink>(&*parsed) : nullptr) {
            configs.push_back(*cfg);
            continue;
        }

        const auto* fileLink = parsed ? std::get_if<ED2KFileLink>(&*parsed) : nullptr;
        if (!fileLink) {
            // Redacted: this list is logged and shown in a dialog, and a malformed
            // config link is the only kind that reaches it still carrying a secret.
            invalid << redactLinkSecret(candidate);
            continue;
        }

        links.push_back({candidate, fileLink->name, md4str(fileLink->hash.data()), fileLink->size});
    }
    return links;
}

/// Queue one download per link. Returns the number of requests sent.
int queueDownloads(const std::vector<ParsedLink>& links, IpcClient* ipc)
{
    for (const ParsedLink& link : links) {
        IpcMessage msg(IpcMsgType::DownloadSearchFile);
        msg.append(link.hashHex);
        msg.append(link.name);
        msg.append(static_cast<qint64>(link.size));
        msg.append(link.rawLine);  // the daemon prefers the raw link (hashset, AICH, sources)
        ipc->sendRequest(std::move(msg));
    }
    return static_cast<int>(links.size());
}

/// Confirm the download of @p links with the user. Always true for Prompt::Silent.
bool confirmDownload(const std::vector<ParsedLink>& links, QWidget* parent,
                     Ed2kLinkImporter::Prompt prompt,
                     const std::function<void()>& beforePrompt)
{
    if (prompt == Ed2kLinkImporter::Prompt::Silent)
        return true;

    if (beforePrompt)
        beforePrompt();

    QStringList names;
    names.reserve(static_cast<qsizetype>(links.size()));
    for (const ParsedLink& link : links)
        names << link.name;

    return QMessageBox::question(
               parent, QObject::tr("eD2K Link"),
               QObject::tr("Do you want to download the following file(s)?\n\n%1")
                   .arg(names.join(QLatin1Char('\n'))),
               QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}

} // anonymous namespace

bool Ed2kLinkImporter::shouldSkip(SearchFile::KnownType type, Source source)
{
    switch (type) {
    // Re-adding these can never do anything useful, whoever asked for it.
    case SearchFile::KnownType::Shared:
    case SearchFile::KnownType::Downloading:
        return true;
    // The file is gone from the transfer list, so asking for it again is a legitimate
    // re-download request — honour it when the user typed or pasted the link themselves,
    // but never let the clipboard watcher start it unprompted.
    case SearchFile::KnownType::Downloaded:
    case SearchFile::KnownType::Cancelled:
        return source == Source::Automatic;
    case SearchFile::KnownType::NotDetermined:
    case SearchFile::KnownType::Unknown:
        break;
    }
    return false;
}

QString Ed2kLinkImporter::skipReason(SearchFile::KnownType type)
{
    switch (type) {
    case SearchFile::KnownType::Shared:      return tr("already shared");
    case SearchFile::KnownType::Downloading: return tr("already downloading");
    case SearchFile::KnownType::Downloaded:  return tr("already downloaded");
    case SearchFile::KnownType::Cancelled:   return tr("previously cancelled");
    default:                                 return {};
    }
}

QString Ed2kLinkImporter::skipMessage(SearchFile::KnownType type, const QString& name)
{
    switch (type) {
    // MFC makes no distinction between a shared file and one that is merely known: either
    // way we already have it (IDS_ERR_ALREADY_DOWNLOADED, srchybrid/DownloadQueue.cpp:332).
    case SearchFile::KnownType::Shared:
    case SearchFile::KnownType::Downloaded:
        return tr("You already have the file \"%1\".").arg(name);
    case SearchFile::KnownType::Downloading:
        return tr("You are already trying to download the file \"%1\".").arg(name);
    case SearchFile::KnownType::Cancelled:
        return tr("You previously cancelled the download of \"%1\".").arg(name);
    case SearchFile::KnownType::NotDetermined:
    case SearchFile::KnownType::Unknown:
        break;
    }
    return {};
}

QStringList Ed2kLinkImporter::splitLinks(const QString& text)
{
    const QString scheme = QStringLiteral("ed2k://");

    QStringList candidates;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        qsizetype start = line.indexOf(scheme, 0, Qt::CaseInsensitive);

        // Nothing that looks like an eD2K link: keep the line whole, so magnet links still
        // parse and anything else is reported as invalid instead of vanishing silently.
        if (start < 0) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty())
                candidates << trimmed;
            continue;
        }

        // A link ends where the next one begins — file names may contain spaces, so the
        // scheme is the only separator we can rely on within a line.
        while (start >= 0) {
            const qsizetype next = line.indexOf(scheme, start + scheme.size(), Qt::CaseInsensitive);
            const QString candidate =
                (next < 0 ? line.mid(start) : line.mid(start, next - start)).trimmed();
            if (!candidate.isEmpty())
                candidates << candidate;
            start = next;
        }
    }
    return candidates;
}

void Ed2kLinkImporter::importLinks(const QString& text, IpcClient* ipc, QWidget* parent,
                                   Source source, Prompt prompt,
                                   std::function<void(const Result&)> done,
                                   std::function<void()> beforePrompt)
{
    Result result;
    std::vector<ED2KHttpCacheLink> configs;
    std::vector<ParsedLink> links = parseLines(text, result.invalid, configs);

    // Configuration links take their own route: they start no download, and they
    // always confirm with the user no matter what @p prompt says — pasting a batch
    // of links is consent to download them, not to store somebody's credential.
    if (!configs.empty()) {
        result.httpCacheConfigs = static_cast<int>(configs.size());
        HttpCacheLinkImporter::apply(configs.front(), ipc, parent, {}, beforePrompt);

        // One at a time: chaining dialogs behind each other is worse than saying so.
        if (configs.size() > 1) {
            logWarning(tr("%n further HTTP Cache link(s) ignored — apply one at a time.",
                          nullptr, static_cast<int>(configs.size()) - 1));
        }
    }

    if (links.empty() || !ipc || !ipc->isConnected()) {
        if (done)
            done(result);
        return;
    }

    // Ask the daemon which of these it already has. Only it can see the known-file and
    // cancelled lists; the GUI models cover downloading and currently-shared files alone.
    QCborArray hashes;
    for (const ParsedLink& link : links)
        hashes.append(link.hashHex);

    IpcMessage msg(IpcMsgType::GetKnownTypes);
    msg.append(hashes);

    const QPointer<QWidget> safeParent(parent);
    ipc->sendRequest(std::move(msg),
        [links = std::move(links), result, ipc, safeParent, source, prompt,
         done = std::move(done), beforePrompt = std::move(beforePrompt)]
        (const IpcMessage& resp) mutable
    {
        if (!ipc->isConnected())
            return;

        // Everything below can open a modal box, and a box must never be opened
        // from inside the call stack that delivered this reply: it spins a nested
        // event loop, and a quit arriving during that loop unwinds every loop at
        // once — main() then destroys the IpcClient and its socket while the
        // socket-read notification is still below us, and Qt returns into freed
        // memory. One turn of the event loop puts the dialogs outside that stack.
        QCborArray types;
        if (resp.fieldBool(0))
            types = resp.fieldArray(1);
        const bool verdictOk = resp.fieldBool(0);

        QTimer::singleShot(0, qApp,
            [links = std::move(links), result, ipc, safeParent, source, prompt, types, verdictOk,
             done = std::move(done), beforePrompt = std::move(beforePrompt)]() mutable
    {
        if (!ipc->isConnected())
            return;

        std::vector<ParsedLink> wanted;
        QString singleSkipMessage;
        const size_t linkCount = links.size();  // links is moved from below

        if (!verdictOk) {
            // No verdict from the daemon. An automatic import must not guess and prompt;
            // a manual one goes ahead unfiltered, and DownloadQueue::addDownloadFromED2KLink
            // still rejects anything already in the transfer list.
            logWarning(QStringLiteral("Ed2kLinkImporter: known-file lookup failed"));
            if (source == Source::Automatic) {
                if (done)
                    done(result);
                return;
            }
            wanted = std::move(links);
        } else {
            wanted.reserve(links.size());

            for (size_t i = 0; i < links.size(); ++i) {
                const auto type = i < static_cast<size_t>(types.size())
                    ? static_cast<SearchFile::KnownType>(types.at(static_cast<qsizetype>(i)).toInteger())
                    : SearchFile::KnownType::Unknown;

                if (!shouldSkip(type, source)) {
                    wanted.push_back(std::move(links[i]));
                    continue;
                }

                ++result.skipped;
                result.skipDescriptions
                    << QStringLiteral("%1 — %2").arg(links[i].name, skipReason(type));
                singleSkipMessage = skipMessage(type, links[i].name);

                // A link the user pasted or clicked that goes nowhere deserves more than an
                // info line; the clipboard watcher is a background convenience and stays quiet.
                if (source == Source::Manual)
                    logError(singleSkipMessage);
                else
                    logInfo(singleSkipMessage);
            }
        }

        // A skipped link surfaces here whoever asked for the import — MFC posts these
        // unconditionally via LOG_STATUSBAR (CDownloadQueue::IsFileExisting,
        // srchybrid/DownloadQueue.cpp:326). One post per import, not one per skipped file,
        // and before the box below so the line is already painted behind it.
        if (result.skipped == 1)
            StatusBarNotifier::post(singleSkipMessage);
        else if (result.skipped > 1)
            StatusBarNotifier::post(tr("%n eD2K link(s) not added — already known",
                                       nullptr, result.skipped));

        // One link, and we are not adding it: saying nothing looks like the paste failed.
        // Several links stay log-only — the survivors are being queued, and a box per skip
        // would be noise.
        if (source == Source::Manual && result.skipped == 1 && linkCount == 1) {
            if (beforePrompt)
                beforePrompt();  // raise the main window; a browser click must not hide this
            QMessageBox::information(safeParent, tr("eD2K Link"), singleSkipMessage);
        }

        if (wanted.empty()) {
            if (done)
                done(result);
            return;
        }

        if (!confirmDownload(wanted, safeParent, prompt, beforePrompt)) {
            if (done)
                done(result);
            return;
        }

        result.added = queueDownloads(wanted, ipc);
        if (done)
            done(result);
        });
    });
}

} // namespace eMule

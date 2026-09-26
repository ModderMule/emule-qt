/// @file IpcClientHandlerMeta.cpp
/// @brief IPC handlers for eNode meta search rows (750-754).
///
/// Torrent/Usenet rows arrive inside eD2K search answers; their metafile lives
/// behind the answering server's Meta API. MetaSearchService does the calls;
/// this file only maps IPC requests onto it.

#include "IpcClientHandler.h"

#include "MetaSearchService.h"
#include "UsenetSession.h"
#include "queue/UsenetQueue.h"

#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "utils/OtherFunctions.h"

#include <QCborMap>
#include <QPointer>

namespace eMule {

using namespace Ipc;

namespace {

/// Largest metafile that fits one IPC frame, with room for the envelope.
constexpr quint32 kMaxIpcMetafile = MaxPayloadSize - 64 * 1024;

/// Failure reply: [false, error, meta].
IpcMessage metaFailure(int seqId, const QString& error, const QCborMap& meta)
{
    IpcMessage m = IpcMessage::makeResult(seqId, false, QCborValue(error));
    m.append(meta);
    return m;
}

/// The row a request names, or nullptr.
const SearchFile* findRow(const IpcMessage& msg)
{
    uint8 hash[16]{};
    if (!theApp.searchList || decodeBase16(msg.fieldString(1), hash, 16) != 16)
        return nullptr;
    const SearchFile* f = theApp.searchList->searchFileByHash(hash, static_cast<uint32>(msg.fieldInt(0)));
    return f && f->isMetaResult() ? f : nullptr;
}

/// "Release.Name" + ".torrent"/".nzb", safe as a file name.
QString suggestedFileName(const SearchFile& f)
{
    QString base = stripInvalidFilenameChars(f.fileName()).trimmed();
    if (base.isEmpty())
        base = md4str(f.fileHash());
    return base + (f.meta().isNzb() ? QStringLiteral(".nzb") : QStringLiteral(".torrent"));
}

} // namespace

void IpcClientHandler::handleFetchMetaFile(const IpcMessage& msg)
{
    const SearchFile* row = findRow(msg);
    if (!row) {
        sendMessage(metaFailure(msg.seqId(), tr("The search result is gone."),
                                MetaSearchService::statusMap(MetaStatus::Error)));
        return;
    }
    auto& svc = MetaSearchService::instance();
    const auto target = svc.targetForResult(*row);
    if (!target) {
        sendMessage(metaFailure(msg.seqId(), tr("The server of this result offers no download service."),
                                MetaSearchService::statusMap(MetaStatus::NoMetaApi)));
        return;
    }

    const QByteArray hash(reinterpret_cast<const char*>(row->fileHash()), 16);
    const QString fileName = suggestedFileName(*row);
    const int kind = static_cast<int>(row->meta().kind);
    QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();

    svc.fetch(*target, hash, row->meta().catalogId, kMaxIpcMetafile,
              [self, seqId, fileName, kind](MetaStatus status, const QString& error, const QCborMap& meta,
                                             const enodemeta::pb::MetaFile& file) {
        if (!self)
            return;
        if (status != MetaStatus::Ok) {
            self->sendMessage(metaFailure(seqId, error, meta));
            return;
        }
        self->sendMessage(IpcMessage::makeResult(seqId, true, QCborMap{
            {QStringLiteral("content"),  file.content()},
            {QStringLiteral("fileName"), fileName},
            {QStringLiteral("kind"),     kind},
        }));
    });
}

void IpcClientHandler::handleDownloadMetaResult(const IpcMessage& msg)
{
    constexpr auto kFailed = static_cast<qint64>(usenet::UsenetAddOutcome::Failed);
    auto refuse = [this, &msg](const QString& error, const QCborMap& meta) {
        IpcMessage m = IpcMessage::makeResult(msg.seqId(), false, QCborValue(error));
        m.append(kFailed);
        m.append(meta);
        sendMessage(std::move(m));
    };

    if (!usenet::theUsenetSession || !usenet::theUsenetSession->queue()) {
        refuse(tr("The Usenet engine is not running."), MetaSearchService::statusMap(MetaStatus::Error));
        return;
    }
    const SearchFile* row = findRow(msg);
    if (!row || !row->meta().isNzb()) {
        refuse(tr("The search result is gone."), MetaSearchService::statusMap(MetaStatus::Error));
        return;
    }

    const bool force = msg.fieldBool(2);
    const int category = int(msg.fieldInt(3));
    const int priority = int(msg.fieldInt(4));
    const bool paused = msg.fieldBool(5);
    if (category < 0 || category >= int(thePrefs.categoryCount())) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Unknown category")));
        return;
    }

    auto& svc = MetaSearchService::instance();
    const auto target = svc.targetForResult(*row);
    if (!target) {
        refuse(tr("The server of this result offers no download service."),
               MetaSearchService::statusMap(MetaStatus::NoMetaApi));
        return;
    }

    const QByteArray hash(reinterpret_cast<const char*>(row->fileHash()), 16);
    const QString name = row->fileName();
    QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();

    svc.fetch(*target, hash, row->meta().catalogId, 0,
              [self, seqId, name, force, category, priority, paused]
              (MetaStatus status, const QString& error, const QCborMap& meta,
               const enodemeta::pb::MetaFile& file) {
        if (!self)
            return;
        if (status != MetaStatus::Ok) {
            IpcMessage m = IpcMessage::makeResult(seqId, false, QCborValue(error));
            m.append(kFailed);
            m.append(meta);
            self->sendMessage(std::move(m));
            return;
        }
        if (!usenet::theUsenetSession || !usenet::theUsenetSession->queue()) {
            IpcMessage m = IpcMessage::makeResult(seqId, false, QCborValue(tr("The Usenet engine is not running.")));
            m.append(kFailed);
            m.append(MetaSearchService::statusMap(MetaStatus::Error));
            self->sendMessage(std::move(m));
            return;
        }

        QString addError;
        usenet::UsenetAddOutcome outcome = usenet::UsenetAddOutcome::Failed;
        const QString itemId = usenet::theUsenetSession->queue()->addNzb(
            file.content(), name, addError,
            {.force = force, .category = category, .priority = priority, .paused = paused},
            &outcome);
        // a search-result grab, counted with the indexer grabs
        self->sendAddNzbResult(seqId, itemId, addError, outcome, usenet::UsenetAddOrigin::IndexerGrab);
    });
}

void IpcClientHandler::handleGetMetaAuthStatus(const IpcMessage& msg)
{
    const QString serverAddr = msg.fieldString(0);
    auto& svc = MetaSearchService::instance();
    const auto target = svc.targetForServer(serverAddr);
    if (!target) {
        sendMessage(metaFailure(msg.seqId(), tr("This server offers no download service."),
                                MetaSearchService::statusMap(MetaStatus::NoMetaApi, serverAddr)));
        return;
    }
    QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();
    svc.authStatus(*target, [self, seqId](bool ok, const QString& error, const QCborMap& meta) {
        if (!self)
            return;
        self->sendMessage(ok ? IpcMessage::makeResult(seqId, true, meta) : metaFailure(seqId, error, meta));
    });
}

void IpcClientHandler::handleMetaLogin(const IpcMessage& msg)
{
    const QString serverAddr = msg.fieldString(0);
    const QString username = msg.fieldString(1);
    const QString password = msg.fieldString(2);
    auto& svc = MetaSearchService::instance();
    const auto target = svc.targetForServer(serverAddr);
    if (!target) {
        sendMessage(metaFailure(msg.seqId(), tr("This server offers no download service."),
                                MetaSearchService::statusMap(MetaStatus::NoMetaApi, serverAddr)));
        return;
    }
    if (username.isEmpty() || password.isEmpty()) {
        sendMessage(metaFailure(msg.seqId(), tr("Enter a user name and a password."),
                                MetaSearchService::statusMap(MetaStatus::AuthRequired, serverAddr)));
        return;
    }
    QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();
    svc.login(*target, username, password, [self, seqId](bool ok, const QString& error, const QCborMap& meta) {
        if (!self)
            return;
        IpcMessage m = IpcMessage::makeResult(seqId, ok, QCborValue(error));
        m.append(meta);
        self->sendMessage(std::move(m));
    });
}

void IpcClientHandler::handleMetaLogout(const IpcMessage& msg)
{
    auto& svc = MetaSearchService::instance();
    const auto target = svc.targetForServer(msg.fieldString(0));
    if (!target) {
        sendMessage(IpcMessage::makeResult(msg.seqId(), false, QCborValue(tr("This server offers no download service."))));
        return;
    }
    QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();
    svc.logout(*target, [self, seqId](bool ok, const QString& error, const QCborMap&) {
        if (self)
            self->sendMessage(IpcMessage::makeResult(seqId, ok, QCborValue(error)));
    });
}

} // namespace eMule

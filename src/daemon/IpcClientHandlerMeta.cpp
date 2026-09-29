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

#include <QCborArray>
#include <QCborMap>
#include <QPointer>

#include <optional>

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

/// What a fetch needs from a meta row, live or restored.
struct MetaRow {
    QByteArray hash16;
    QString catalogId;
    QString name;
    enodemeta::Kind kind = enodemeta::Kind::Unspecified;
    std::optional<MetaSearchService::Target> target;

    [[nodiscard]] bool isNzb() const { return kind == enodemeta::Kind::Nzb; }
};

/// "Release.Name" + ".torrent"/".nzb", safe as a file name.
QString suggestedFileName(const MetaRow& r)
{
    QString base = stripInvalidFilenameChars(r.name).trimmed();
    if (base.isEmpty())
        base = md4str(reinterpret_cast<const uint8*>(r.hash16.constData()));
    return base + (r.isNzb() ? QStringLiteral(".nzb") : QStringLiteral(".torrent"));
}

/// The row a request names: the live search's, else the one the GUI stored
/// (field @p rowField) — its search died with the last restart.
std::optional<MetaRow> findRow(const IpcMessage& msg, int rowField)
{
    uint8 hash[16]{};
    if (decodeBase16(msg.fieldString(1), hash, 16) != 16)
        return std::nullopt;

    MetaRow r;
    r.hash16 = QByteArray(reinterpret_cast<const char*>(hash), 16);
    auto& svc = MetaSearchService::instance();

    if (theApp.searchList) {
        const SearchFile* f = theApp.searchList->searchFileByHash(hash, static_cast<uint32>(msg.fieldInt(0)));
        if (f && f->isMetaResult()) {
            r.catalogId = f->meta().catalogId;
            r.name = f->fileName();
            r.kind = f->meta().kind;
            r.target = svc.targetForResult(*f);
            return r;
        }
    }

    const QCborMap stored = msg.fieldMap(rowField);
    r.kind = static_cast<enodemeta::Kind>(stored.value(QStringLiteral("metaKind")).toInteger());
    if (r.kind == enodemeta::Kind::Unspecified)
        return std::nullopt;
    r.catalogId = stored.value(QStringLiteral("metaCatalogId")).toString();
    r.name = stored.value(QStringLiteral("name")).toString();
    std::list<SearchFile::SServer> servers;
    for (const QCborValue& v : stored.value(QStringLiteral("metaServers")).toArray()) {
        const QCborArray pair = v.toArray();
        servers.push_back({.ip = static_cast<uint32>(pair.at(0).toInteger()),
                           .port = static_cast<uint16>(pair.at(1).toInteger())});
    }
    r.target = svc.targetForServers(servers);
    return r;
}

} // namespace

void IpcClientHandler::handleFetchMetaFile(const IpcMessage& msg)
{
    const auto row = findRow(msg, 2);
    if (!row) {
        sendMessage(metaFailure(msg.seqId(), tr("The search result is gone."),
                                MetaSearchService::statusMap(MetaStatus::Error)));
        return;
    }
    if (!row->target) {
        sendMessage(metaFailure(msg.seqId(), tr("The server of this result offers no download service."),
                                MetaSearchService::statusMap(MetaStatus::NoMetaApi)));
        return;
    }

    const QString fileName = suggestedFileName(*row);
    const int kind = static_cast<int>(row->kind);
    QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();

    MetaSearchService::instance().fetch(*row->target, row->hash16, row->catalogId, kMaxIpcMetafile,
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
    const auto row = findRow(msg, 6);
    if (!row || !row->isNzb()) {
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

    if (!row->target) {
        refuse(tr("The server of this result offers no download service."),
               MetaSearchService::statusMap(MetaStatus::NoMetaApi));
        return;
    }

    const QString name = row->name;
    QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();

    MetaSearchService::instance().fetch(*row->target, row->hash16, row->catalogId, 0,
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

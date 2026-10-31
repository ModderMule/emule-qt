/// @file IpcClientHandlerMeta.cpp
/// @brief IPC handlers for eNode meta search rows (750-754).
///
/// Torrent/Usenet/web/container rows arrive inside eD2K search answers; their metafile lives
/// behind the answering server's Meta API. MetaSearchService does the calls;
/// this file only maps IPC requests onto it.

#include "IpcClientHandler.h"

#include "BtBridge.h"
#include "MetaSearchService.h"
#include "UsenetBridge.h"
#include "UsenetSession.h"
#include "nzb/NzbFile.h"
#include "queue/UsenetQueue.h"

#include "app/AppContext.h"
#include "enodemeta/LinkContainer.h"
#include "enodemeta/WebLinks.h"
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "utils/OtherFunctions.h"

#include <QCborArray>
#include <QCborMap>
#include <QPointer>
#include <QSet>

#include <algorithm>
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
    uint32 fileIndex = enodemeta::kFileIndexWholeSet32;   ///< file row's file, else whole set
    QString filePath;   ///< FT_META_FILEPATH: an NZB's subject, over the index

    [[nodiscard]] bool isNzb() const { return kind == enodemeta::Kind::Nzb; }
    [[nodiscard]] bool isWholeSet() const { return fileIndex == enodemeta::kFileIndexWholeSet32; }
};

/// File index from meta-hash bytes 4..5 (uint16 LE), for a row with no SearchFile.
uint32 hashFileIndex(const QByteArray& hash16)
{
    const auto idx = static_cast<uint16>(uint8(hash16.at(4)) | (uint8(hash16.at(5)) << 8));
    return idx == enodemeta::kFileIndexWholeSet ? enodemeta::kFileIndexWholeSet32 : idx;
}

/// The NZB file a file row stands for: subject first (NZB hashes are path
/// authoritative), then the index. nullopt: whole set, a par2 file, or no match.
std::optional<int> resolveNzbFile(const usenet::NzbInfo& nzb, const MetaRow& r)
{
    if (r.isWholeSet())
        return std::nullopt;
    std::optional<int> hit;
    if (!r.filePath.isEmpty()) {
        for (int f = 0; f < nzb.files.size() && !hit; ++f)
            if (nzb.files.at(f).subject == r.filePath)
                hit = f;
    }
    if (!hit && r.fileIndex < uint32(nzb.files.size()))
        hit = int(r.fileIndex);
    if (hit && nzb.files.at(*hit).isPar2())
        return std::nullopt;
    return hit;
}

/// "Release.Name" + the kind's extension, safe as a file name. A container is
/// named by the format its content type says (enodemeta amendment 27).
QString suggestedFileName(const MetaRow& r, const QString& contentType)
{
    QString base = stripInvalidFilenameChars(r.name).trimmed();
    if (base.isEmpty())
        base = md4str(reinterpret_cast<const uint8*>(r.hash16.constData()));
    switch (r.kind) {
    case enodemeta::Kind::Nzb:
        return base + QStringLiteral(".nzb");
    case enodemeta::Kind::Web:
        return base + QStringLiteral(".txt");
    case enodemeta::Kind::Container: {
        QString format = enodemeta::containerFormatOf(contentType);
        const QString own = enodemeta::containerFormat(base);
        if (format.isEmpty())
            format = own.isEmpty() ? QStringLiteral("dlc") : own;
        if (!own.isEmpty())
            base.chop(own.size() + 1);
        return base + u'.' + format;
    }
    default:
        return base + QStringLiteral(".torrent");
    }
}

/// A web row's links, parsed for the GUI's dialog.
QCborMap webLinksMap(const QByteArray& content)
{
    const auto set = enodemeta::parseWebLinks(content);
    if (!set)
        return {};
    QCborArray links;
    for (const enodemeta::WebLink& l : set->links) {
        links.append(QCborMap{
            {QStringLiteral("url"),    QString::fromUtf8(l.url)},
            {QStringLiteral("file"),   static_cast<qint64>(l.file)},
            {QStringLiteral("name"),   l.name},
            {QStringLiteral("size"),   static_cast<qint64>(l.size)},
            {QStringLiteral("hoster"), l.hoster},
        });
    }
    return {{QStringLiteral("title"), set->title},
            {QStringLiteral("protected"), set->isProtected},
            {QStringLiteral("links"), links}};
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
            r.fileIndex = f->meta().fileIndex;
            r.filePath = f->meta().filePath;
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
    r.fileIndex = hashFileIndex(r.hash16);
    return r;
}

/// A sibling row of the same search, by hash: live only, else from its hash.
MetaRow siblingRow(const QString& hashHex, uint32 searchID)
{
    MetaRow r;
    uint8 hash[16]{};
    if (decodeBase16(hashHex, hash, 16) != 16)
        return r;   // whole set: keeps everything
    r.hash16 = QByteArray(reinterpret_cast<const char*>(hash), 16);
    r.fileIndex = hashFileIndex(r.hash16);
    if (theApp.searchList) {
        if (const SearchFile* f = theApp.searchList->searchFileByHash(hash, searchID); f && f->isMetaResult()) {
            r.fileIndex = f->meta().fileIndex;
            r.filePath = f->meta().filePath;
        }
    }
    return r;
}

/// Queue name for an NZB row: a file row takes its whole-set row's name, while
/// the search is live. Else the row's own name.
QString releaseName(const MetaRow& row, uint32 searchID)
{
    if (row.isWholeSet() || row.hash16.size() != 16 || !theApp.searchList)
        return row.name;
    QByteArray whole = row.hash16;
    whole[4] = char(0xFF);
    whole[5] = char(0xFF);
    const SearchFile* f = theApp.searchList->searchFileByHash(
        reinterpret_cast<const uint8*>(whole.constData()), searchID);
    return f && f->isMetaResult() && !f->fileName().isEmpty() ? f->fileName() : row.name;
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

    QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();
    // field 3: also parse it, for the search row's Details
    const bool inspect = msg.fieldBool(3);

    MetaSearchService::instance().fetch(*row->target, row->hash16, row->catalogId, kMaxIpcMetafile,
              [self, seqId, inspect, r = *row](MetaStatus status, const QString& error, const QCborMap& meta,
                                       const enodemeta::pb::MetaFile& file) {
        if (!self)
            return;
        if (status != MetaStatus::Ok) {
            self->sendMessage(metaFailure(seqId, error, meta));
            return;
        }
        QCborMap reply{
            {QStringLiteral("content"),     file.content()},
            {QStringLiteral("fileName"),    suggestedFileName(r, file.contentType())},
            {QStringLiteral("kind"),        static_cast<int>(r.kind)},
            {QStringLiteral("contentType"), file.contentType()},
        };
        // verified already, so it parses; the GUI never reads a uri-list itself
        if (r.kind == enodemeta::Kind::Web)
            reply.insert(QStringLiteral("links"), webLinksMap(file.content()));
        if (inspect) {
            QString inspectError;
            QCborMap inspection;
            if (r.kind == enodemeta::Kind::BtV1 || r.kind == enodemeta::Kind::BtV2)
                inspection = BtBridge::inspectTorrent(file.content(), inspectError);
            else if (r.kind == enodemeta::Kind::Nzb)
                inspection = UsenetBridge::inspectNzb(file.content(), {}, inspectError);
            if (r.kind == enodemeta::Kind::Nzb) {
                // which file opened the Details window; -1 whole set or unknown
                usenet::NzbInfo nzb;
                QString parseError;
                const auto hit = usenet::NzbFile::parse(file.content(), nzb, parseError) ? resolveNzbFile(nzb, r)
                                                                                 : std::nullopt;
                reply.insert(QStringLiteral("rowFileIndex"), hit.value_or(-1));
            }
            if (!inspection.isEmpty())
                reply.insert(QStringLiteral("inspection"), inspection);
            else if (!inspectError.isEmpty())
                reply.insert(QStringLiteral("inspectError"), inspectError);
        }
        self->sendMessage(IpcMessage::makeResult(seqId, true, reply));
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

    // field 8: files unchecked in the Details window, authoritative when present
    // (empty = all). Else, with usenetMetaSelectedFileOnly, the file rows picked
    // (this row plus field 7's siblings of the same NZB); a whole-set row = all.
    std::optional<QSet<int>> unchecked;
    if (msg.fieldCount() > 8 && msg.field(8).isArray()) {
        QSet<int> skip;
        for (const QCborValue& v : msg.field(8).toArray())
            skip.insert(int(v.toInteger(-1)));
        unchecked = skip;
    }
    std::vector<MetaRow> picked;
    if (!unchecked && thePrefs.usenetMetaSelectedFileOnly()) {
        picked.push_back(*row);
        const auto searchID = static_cast<uint32>(msg.fieldInt(0));
        for (const QCborValue& v : msg.field(7).toArray())
            picked.push_back(siblingRow(v.toString(), searchID));
        if (std::ranges::any_of(picked, &MetaRow::isWholeSet))
            picked.clear();
    }

    const QString name = releaseName(*row, static_cast<uint32>(msg.fieldInt(0)));
    QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();

    MetaSearchService::instance().fetch(*row->target, row->hash16, row->catalogId, 0,
              [self, seqId, name, force, category, priority, paused, unchecked, picked]
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

        // as kept files, not skipped ones: a release still arriving can take them in
        QList<int> kept;
        if ((unchecked && !unchecked->isEmpty()) || !picked.empty()) {
            usenet::NzbInfo nzb;
            QString parseError;
            if (usenet::NzbFile::parse(file.content(), nzb, parseError)) {
                if (unchecked) {
                    for (int f = 0; f < nzb.files.size(); ++f)
                        if (!nzb.files.at(f).isPar2() && !unchecked->contains(f))
                            kept.append(f);
                } else {
                    for (const MetaRow& r : picked)
                        if (const auto f = resolveNzbFile(nzb, r))
                            kept.append(*f);
                }
            }
        }

        QString addError;
        usenet::UsenetAddOutcome outcome = usenet::UsenetAddOutcome::Failed;
        const QString itemId = usenet::theUsenetSession->queue()->addNzb(
            file.content(), name, addError,
            {.force = force, .category = category, .priority = priority, .paused = paused,
             .keptFiles = kept},
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

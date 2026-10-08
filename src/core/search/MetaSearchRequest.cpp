#include "pch.h"
/// @file MetaSearchRequest.cpp
/// @brief A Usenet / torrent search through an eD2K server's Meta API.

#include "search/MetaSearchRequest.h"

#include "app/AppContext.h"
#include "enodemeta/MetaHash.h"
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QCoreApplication>

namespace eMule {

namespace pb = enodemeta::pb;
using pb::MetaKindGadget::MetaKind;
using pb::MetaNetworkGadget::MetaNetwork;

namespace {

// the server cuts it to its own cap (eNode: 100 unless configured higher).
// A torrent release is a row a file, so it asks for fewer.
constexpr uint32 kPageReleasesUsenet = 500;
constexpr uint32 kPageReleasesTorrent = 200;

MetaSearchCandidate candidateFor(const Server& srv)
{
    MetaSearchCandidate c;
    c.endpoint.baseUrl = srv.metaApiUrl();
    c.endpoint.pin = srv.metaApiPin();
    c.endpoint.serverName = srv.name().isEmpty() ? srv.address() : srv.name();
    c.serverAddr = srv.addressWithPort();
    c.serverId = srv.serverId();
    c.ip = srv.ipAddress().isIPv4() ? srv.ipAddress().toNetworkUint32() : 0;
    c.port = srv.port();
    return c;
}

/// Words of an expression; a quoted run is one word. Brackets come back as words.
QStringList expressionWords(const QString& expression)
{
    QStringList words;
    QString cur;
    bool quoted = false;
    auto flush = [&] {
        if (!cur.isEmpty())
            words.append(std::exchange(cur, {}));
    };
    for (const QChar ch : expression) {
        if (ch == u'"') {
            quoted = !quoted;
            if (!quoted)
                flush();
        } else if (quoted) {
            cur.append(ch);
        } else if (ch.isSpace()) {
            flush();
        } else if (ch == u'(' || ch == u')') {
            flush();
            words.append(QString(ch));
        } else {
            cur.append(ch);
        }
    }
    flush();
    return words;
}

bool endsWithExtension(const QString& name, const QString& ext)
{
    return name.endsWith(u'.' + ext, Qt::CaseInsensitive);
}

} // namespace

uint32 metaNetworkFor(SearchType type)
{
    switch (type) {
    case SearchType::MetaUsenet:  return static_cast<uint32>(MetaNetwork::META_NETWORK_USENET);
    case SearchType::MetaTorrent: return static_cast<uint32>(MetaNetwork::META_NETWORK_TORRENT);
    default:                      return 0;
    }
}

QString metaNetworkName(SearchType type)
{
    return type == SearchType::MetaUsenet
        ? QCoreApplication::translate("eMule::IpcClientHandler", "Usenet")
        : QCoreApplication::translate("eMule::IpcClientHandler", "torrent");
}

std::vector<MetaSearchCandidate>
metaSearchCandidates(const ServerList& list, uint32 network, const MetaCandidateOrder& order)
{
    std::vector<MetaSearchCandidate> out;
    // The connected server answers first whatever its priority: it is the one the
    // user chose to be on.
    const Server* connected = order.connectedServerId != 0
        ? list.findById(order.connectedServerId) : nullptr;
    if (connected && connected->mayServeMetaNetwork(network))
        out.push_back(candidateFor(*connected));

    const std::vector<Server*> ordered = list.autoConnectOrder(order.usePriorities, order.staticOnly);
    for (const Server* srv : ordered) {
        if (srv != connected && srv->mayServeMetaNetwork(network))
            out.push_back(candidateFor(*srv));
    }
    // Last, the ones that said no when last asked: an operator may have switched
    // the catalogue on since, and nothing else would ever ask them again.
    if (connected && connected->hasMetaApi() && !connected->mayServeMetaNetwork(network))
        out.push_back(candidateFor(*connected));
    for (const Server* srv : ordered) {
        if (srv != connected && srv->hasMetaApi() && !srv->mayServeMetaNetwork(network))
            out.push_back(candidateFor(*srv));
    }
    return out;
}

std::vector<MetaSearchCandidate> metaSearchCandidates(SearchType type)
{
    if (!theApp.serverList)
        return {};
    MetaCandidateOrder order;
    if (theApp.serverConnect && theApp.serverConnect->isConnected()) {
        if (const Server* cur = theApp.serverConnect->currentServer())
            order.connectedServerId = cur->serverId();
    }
    order.usePriorities = thePrefs.useServerPriorities();
    order.staticOnly = thePrefs.autoConnectStaticOnly();
    return metaSearchCandidates(*theApp.serverList, metaNetworkFor(type), order);
}

bool metaServerInfoPending()
{
    if (!theApp.serverConnect)
        return false;
    if (theApp.serverConnect->isConnecting())
        return true;
    if (!theApp.serverConnect->isConnected())
        return false;
    // the ident, and the Meta API with it, comes a moment after the login
    const Server* cur = theApp.serverConnect->currentServer();
    return cur && !cur->hasServerHash();
}

std::expected<pb::SearchRequest, QString>
buildMetaSearchRequest(const SearchParams& params, SearchType type)
{
    QStringList keywords;
    QStringList exclude;
    bool negateNext = false;
    for (const QString& word : expressionWords(params.expression)) {
        if (word == u"(" || word == u")" || word == u"OR") {
            return std::unexpected(QCoreApplication::translate(
                "eMule::IpcClientHandler",
                "A %1 search through a server takes keywords and NOT only — no OR and no brackets.")
                    .arg(metaNetworkName(type)));
        }
        if (word == u"AND")
            continue;
        if (word == u"NOT") {
            negateNext = true;
            continue;
        }
        if (word.size() > 1 && word.startsWith(u'-'))
            exclude.append(word.mid(1));
        else if (std::exchange(negateNext, false))
            exclude.append(word);
        else
            keywords.append(word);
        negateNext = false;
    }
    if (keywords.isEmpty()) {
        return std::unexpected(QCoreApplication::translate(
            "eMule::IpcClientHandler", "The search expression contains nothing to search for."));
    }

    pb::SearchRequest req;
    req.setQuery(keywords.join(u' '));
    req.setExclude(exclude);
    req.setNetwork(static_cast<MetaNetwork>(metaNetworkFor(type)));
    // The catalogue's own type names — not the eD2K search term, which folds
    // archives and CD images into "Pro". Anything else it would match nothing on.
    static const QStringList kTypes{QStringLiteral(ED2KFTSTR_AUDIO), QStringLiteral(ED2KFTSTR_VIDEO),
                                    QStringLiteral(ED2KFTSTR_IMAGE), QStringLiteral(ED2KFTSTR_DOCUMENT),
                                    QStringLiteral(ED2KFTSTR_PROGRAM), QStringLiteral(ED2KFTSTR_ARCHIVE),
                                    QStringLiteral(ED2KFTSTR_CDIMAGE)};
    if (kTypes.contains(params.fileType))
        req.setType(params.fileType);
    req.setMinSize(params.minSize);
    req.setMaxSize(params.maxSize);
    // availability means sources; only a torrent has any
    if (type == SearchType::MetaTorrent)
        req.setMinSeeders(params.availability);
    req.setLimit(type == SearchType::MetaTorrent ? kPageReleasesTorrent : kPageReleasesUsenet);
    return req;
}

std::unique_ptr<SearchFile>
searchFileFromMetaEntry(const pb::MetaEntry& entry, SearchType type, const SearchParams& params,
                        uint32 serverIP, uint16 serverPort)
{
    const MetaKind kind = entry.kind();
    const bool torrent = kind == MetaKind::META_KIND_BT_V1 || kind == MetaKind::META_KIND_BT_V2;
    const bool wanted = type == SearchType::MetaUsenet ? kind == MetaKind::META_KIND_NZB : torrent;
    if (!wanted || entry.metaHash().size() != enodemeta::kMetaHashSize || entry.name().isEmpty())
        return nullptr;

    const bool wholeRelease = entry.fileIndex() == enodemeta::kFileIndexWholeSet32;
    if (!params.extension.isEmpty() && !wholeRelease
        && !endsWithExtension(entry.name(), params.extension)
        && !endsWithExtension(entry.filePath(), params.extension))
        return nullptr;

    // Written as the search result record an eD2K answer carries and read back
    // through the one parser: the hash and tag checks, the name prefix and what a
    // stored search keeps are the same for a row from either way in.
    SafeMemFile rec;
    rec.write(entry.metaHash().constData(), enodemeta::kMetaHashSize);
    rec.writeUInt32(0);   // no client: the row is a release, not a source
    rec.writeUInt16(0);
    const qint64 countPos = rec.position();
    rec.writeUInt32(0);

    uint32 count = 0;
    auto put = [&rec, &count](const Tag& tag) {
        tag.writeNewEd2kTag(rec, UTF8Mode::Raw);
        ++count;
    };
    put(Tag(FT_FILENAME, entry.name()));
    put(Tag(FT_FILESIZE, static_cast<uint64>(entry.size())));
    if (!entry.type().isEmpty())
        put(Tag(FT_FILETYPE, entry.type()));
    if (torrent && entry.seeders() != 0)
        put(Tag(FT_SOURCES, static_cast<uint32>(entry.seeders())));
    put(Tag(FT_META_KIND, static_cast<uint32>(kind)));
    put(Tag(FT_META_FILEINDEX, static_cast<uint32>(entry.fileIndex())));
    if (!entry.filePath().isEmpty())
        put(Tag(FT_META_FILEPATH, entry.filePath()));
    if (entry.totalSize() != 0)
        put(Tag(FT_META_TOTALSIZE, static_cast<uint64>(entry.totalSize())));
    if (!entry.catalogId().isEmpty())
        put(Tag(FT_META_ID, entry.catalogId()));
    if (entry.seeders() != 0)
        put(Tag(FT_META_SEEDERS, static_cast<uint32>(entry.seeders())));
    if (entry.peers() != 0)
        put(Tag(FT_META_PEERS, static_cast<uint32>(entry.peers())));
    if (entry.ageDays() != 0)
        put(Tag(FT_META_AGE, static_cast<uint32>(entry.ageDays())));
    if (!entry.indexer().isEmpty())
        put(Tag(FT_META_INDEXER, entry.indexer()));
    if (entry.flags() != 0)
        put(Tag(FT_META_FLAGS, static_cast<uint32>(entry.flags())));
    if (!entry.magnet().isEmpty())
        put(Tag(FT_META_MAGNET, entry.magnet()));

    rec.seek(countPos, 0);
    rec.writeUInt32(count);
    rec.seek(0, 0);

    std::unique_ptr<SearchFile> file;
    try {
        file = std::make_unique<SearchFile>(rec, true, serverIP, serverPort);
    } catch (const FileException&) {
        return nullptr;
    }
    // the hash has to say the same as the entry, or the row is not what it claims
    if (!file->isMetaResult() || file->isInvalidMetaResult())
        return nullptr;
    return file;
}

} // namespace eMule

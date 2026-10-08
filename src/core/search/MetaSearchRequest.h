#pragma once

/// @file MetaSearchRequest.h
/// @brief A Usenet / torrent search through an eD2K server's Meta API.
///
/// The "Usenet (Server)" and "Torrent (Server)" methods ask MetaApi.Search of an
/// eNode server (enodemeta api.proto) for one network. This file is the part
/// without a socket: which servers to ask and in what order, the request for a
/// set of search parameters, and a result row out of a MetaEntry.

#include "enodemeta/MetaApiClient.h"
#include "search/SearchParams.h"
#include "utils/Types.h"

#include <QString>

#include <expected>
#include <memory>
#include <vector>

namespace eMule {

class SearchFile;
class Server;
class ServerList;

/// The MetaNetwork value a meta search type asks for; 0 for any other type.
[[nodiscard]] uint32 metaNetworkFor(SearchType type);

/// "Usenet" / "torrent", for messages.
[[nodiscard]] QString metaNetworkName(SearchType type);

/// A server whose Meta API may be asked, copied out of the list so an answer
/// arriving later does not hold a Server pointer.
struct MetaSearchCandidate {
    enodemeta::MetaEndpoint endpoint;
    QString serverAddr;     ///< "addr:port"
    uint32 serverId = 0;
    uint32 ip = 0;          ///< ed2k form, 0 for an IPv6-only server
    uint16 port = 0;
};

/// How candidates are ordered.
struct MetaCandidateOrder {
    uint32 connectedServerId = 0;   ///< asked first when it has a Meta API; 0 = none
    bool usePriorities = true;
    bool staticOnly = false;
};

/// The servers to ask for @p network, best first: the connected one, then the
/// rest in the order auto-connect dials them. Only servers with a known Meta
/// API; those that lacked the network when last asked go to the end.
[[nodiscard]] std::vector<MetaSearchCandidate>
metaSearchCandidates(const ServerList& list, uint32 network, const MetaCandidateOrder& order);

/// Same, from the running client's list, connection and preferences.
[[nodiscard]] std::vector<MetaSearchCandidate> metaSearchCandidates(SearchType type);

/// Whether a server may still tell us it has a Meta API: one is being dialed, or
/// the connected one has not sent its ident yet.
[[nodiscard]] bool metaServerInfoPending();

/// The request for a search. Fails with a message for an expression the Meta
/// API cannot take (it knows keywords and exclusions, no OR and no brackets).
[[nodiscard]] std::expected<enodemeta::pb::SearchRequest, QString>
buildMetaSearchRequest(const SearchParams& params, SearchType type);

/// A result row from an entry of the asked network, attributed to the answering
/// server. Null for an entry of another kind, one without a usable hash, or one
/// the extension filter of @p params drops.
[[nodiscard]] std::unique_ptr<SearchFile>
searchFileFromMetaEntry(const enodemeta::pb::MetaEntry& entry, SearchType type,
                        const SearchParams& params, uint32 serverIP, uint16 serverPort);

} // namespace eMule

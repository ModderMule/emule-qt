#include "pch.h"
/// @file SearchParams.cpp
/// @brief Search query parameters — port of MFC SearchParams.

#include "search/SearchParams.h"
#include "utils/SafeFile.h"

namespace eMule {

// ---------------------------------------------------------------------------
// Automatic search-method resolution
// ---------------------------------------------------------------------------

std::optional<SearchType> resolveAutomaticSearchType(const AutoSearchState& state)
{
    // Easy if only one network is up.
    if (!state.serverConnected && state.kadConnected)
        return SearchType::Kademlia;
    if (state.serverConnected && !state.kadConnected)
        return SearchType::Ed2kServer;
    if (!state.serverConnected && !state.kadConnected)
        return std::nullopt;

    // Connected to both. We choose Kad, except
    // - if we are connected to a static server
    // - or a server with more than 40k and less than 2mio users connected,
    //      more than 5 mio files and if our serverlist contains less than 40 servers
    //      (otherwise we have assume that its polluted with fake servers and we might
    //      just as well to be connected to one)
    // might be further optimized in the future
    const bool preferServer = state.serverIsStatic
                              || (state.serverUsers > 40000
                                  && state.serverCount < 40
                                  && state.serverUsers < 2000000 //was 5M - copy & paste bug
                                  && state.serverFiles > 5000000);
    return preferServer ? SearchType::Ed2kServer : SearchType::Kademlia;
}

// ---------------------------------------------------------------------------
// Persistence — partial serialization for search tab state
// ---------------------------------------------------------------------------

SearchParams::SearchParams(FileDataIO& file)
{
    searchID = file.readUInt32();
    type = static_cast<SearchType>(file.readUInt8());
    clientSharedFiles = file.readUInt8() != 0;
    specialTitle = file.readString(true);
    expression = file.readString(true);
    fileType = file.readString(true);
}

void SearchParams::storePartially(FileDataIO& file) const
{
    file.writeUInt32(searchID);
    file.writeUInt8(static_cast<uint8>(type));
    file.writeUInt8(clientSharedFiles ? 1 : 0);
    file.writeString(specialTitle, UTF8Mode::Raw);
    file.writeString(expression, UTF8Mode::Raw);
    file.writeString(fileType, UTF8Mode::Raw);
}

} // namespace eMule

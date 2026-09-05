# ---------------------------------------------------------------------------
# libemuleindexer — the shared newznab / torznab search client
#
# A peer of eMule::Core, eMule::Ipc and eMule::Usenet, and part of none of them.
#
# newznab (Usenet) and torznab (BitTorrent) are the same HTTP API with a
# different XML attribute namespace, and Prowlarr and NZBHydra2 serve both from
# one endpoint. Writing two clients would be writing the same QXmlStreamReader
# parser twice, so there is one, and it belongs to neither network.
#
# Dependency direction: indexer -> core is fine (Log, Preferences, HttpDefaults,
# AesCbc). indexer -> usenet must NEVER happen, or a future BitTorrent module
# could not reuse this. The daemon is the glue: it owns the search list and
# routes a grab into whichever engine the result belongs to.
#
# See docs/BitTorrentModule-Research.local.md §7.3 for the decision.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE INDEXER_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

add_library(emuleindexer STATIC ${INDEXER_SOURCES})
add_library(eMule::Indexer ALIAS emuleindexer)

target_include_directories(emuleindexer
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(emuleindexer
    PUBLIC
        Qt6::Core
        Qt6::Network
        eMule::Core
        eMule::CommonSettings
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${INDEXER_SOURCES})

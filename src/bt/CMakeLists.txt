# ---------------------------------------------------------------------------
# libemulebt — BitTorrent download engine (libtorrent-rasterbar)
#
# A peer of eMule::Core and eMule::Usenet. bt -> core is fine (Log, Preferences
# values via BtSettings); core -> bt must never happen. Only emulecored links this;
# the GUI talks IPC. No libtorrent type outside src/bt (research doc §4): the public
# headers are BtSession.h, BtTypes.h and BtIds.h.
#
# See docs/BitTorrentModule-Research.local.md and libs/packmule/docs/torrent-join.md.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE BT_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

add_library(emulebt STATIC ${BT_SOURCES})
add_library(eMule::Bt ALIAS emulebt)

target_include_directories(emulebt
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(emulebt
    PUBLIC
        Qt6::Core
        eMule::Core
        eMule::CommonSettings
        # PUBLIC: the target carries TORRENT_ABI_VERSION and the other defines to every
        # unit that includes a libtorrent header (research doc §5.5 trap 3)
        LibtorrentRasterbar::torrent-rasterbar
)

target_compile_definitions(emulebt PUBLIC EMULE_HAVE_BITTORRENT=1)

set_target_properties(emulebt PROPERTIES FOLDER "Libraries")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${BT_SOURCES})

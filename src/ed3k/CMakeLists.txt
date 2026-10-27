# ---------------------------------------------------------------------------
# libemuleed3k — the ed3k node (packmule, Rust) in the daemon
#
# A peer of eMule::Core, eMule::Usenet and eMule::Bt. ed3k -> core is fine (Log,
# Preferences); core -> ed3k must never happen. Only emulecored links this; the
# GUI talks IPC. packmule::ffi comes from libs/packmule/crates/packmule-ffi
# (top-level EMULE_ED3K). See docs/ed3k-module.md.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE ED3K_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

add_library(emuleed3k STATIC ${ED3K_SOURCES})
add_library(eMule::Ed3k ALIAS emuleed3k)

target_include_directories(emuleed3k
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(emuleed3k
    PUBLIC
        Qt6::Core
        Qt6::Network
        eMule::Core
        # Ed3kUsenetJoin: the usenet join (usenet-join-host.md); ed3k -> usenet only
        eMule::Usenet
        # PUBLIC: Ed3kSession.h hands out ed3k::Engine (E2: usenet join)
        packmule::ffi
    PRIVATE
        OpenSSL::Crypto
)

set_target_properties(emuleed3k PROPERTIES FOLDER "Libraries")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ED3K_SOURCES})

# ---------------------------------------------------------------------------
# libemuleusenet — Usenet (NNTP) download engine
#
# A peer of eMule::Core and eMule::Ipc, not a part of either.
#
# Dependency direction is one-way: usenet -> core is fine (Log, Preferences,
# ArchiveReader), core -> usenet must never happen.  Only emulecored links this;
# the GUI stays daemon-agnostic and talks IPC.
#
# See docs/UsenetModule-Research.local.md for the design this implements.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE USENET_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

add_library(emuleusenet STATIC ${USENET_SOURCES})
add_library(eMule::Usenet ALIAS emuleusenet)

target_include_directories(emuleusenet
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(emuleusenet
    PUBLIC
        Qt6::Core
        Qt6::Network
        OpenSSL::SSL
        OpenSSL::Crypto
        eMule::Core
        eMule::CommonSettings
)

if(EMULE_USENET_RAPIDYENC AND TARGET rapidyenc_static)
    target_link_libraries(emuleusenet PRIVATE rapidyenc_static)
    target_compile_definitions(emuleusenet PRIVATE EMULE_HAVE_RAPIDYENC=1)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${USENET_SOURCES})

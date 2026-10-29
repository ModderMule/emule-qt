cmake_minimum_required(VERSION 3.25)

project(eMuleQt
    VERSION 0.7.0
    DESCRIPTION "eMule P2P file sharing client — Qt 6 port"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Qt 6 discovery — hint paths for local Qt installations
# ---------------------------------------------------------------------------
if(WIN32)
    foreach(_qt_ver 6.10.2 6.10.1 6.10.0 6.9.0 6.8.0)
        list(APPEND _qt_hints "C:/Qt/${_qt_ver}/msvc2022_64")
        list(APPEND _qt_hints "$ENV{USERPROFILE}/Qt/${_qt_ver}/msvc2022_64")
    endforeach()
elseif(APPLE)
    list(APPEND _qt_hints "$ENV{HOME}/Qt/6.10.2/macos")
else()
    list(APPEND _qt_hints "$ENV{HOME}/Qt/6.10.2/gcc_64")
endif()
foreach(_hint IN LISTS _qt_hints)
    if(EXISTS "${_hint}")
        list(PREPEND CMAKE_PREFIX_PATH "${_hint}")
        break()
    endif()
endforeach()

# Linux: link OpenGL as libGL.so.1 (what the Qt libs need anyway). GLVND default
# adds libOpenGL.so.0 + libGLX.so.0, and libopengl0 is missing on many desktops
# -> v0.5.2 tarball failed to start there.
if(UNIX AND NOT APPLE)
    set(OpenGL_GL_PREFERENCE LEGACY)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Multimedia
    HttpServer
    Protobuf
    ProtobufTools
    Sql         # qtbase; the bundled SQLite driver backs the seen-files index
    Test
)

qt_standard_project_setup()

# ---------------------------------------------------------------------------
# Global C++23 settings and compiler hardening
# ---------------------------------------------------------------------------
include(cmake/CompilerSettings.cmake)

# ---------------------------------------------------------------------------
# Platform feature detection → generates config.h
# ---------------------------------------------------------------------------
include(CheckIncludeFileCXX)
include(CheckSymbolExists)
include(CMakePushCheckState)

check_include_file_cxx("sys/socket.h"  HAVE_SYS_SOCKET_H)
check_include_file_cxx("netinet/in.h"  HAVE_NETINET_IN_H)
check_include_file_cxx("arpa/inet.h"   HAVE_ARPA_INET_H)
check_include_file_cxx("unistd.h"      HAVE_UNISTD_H)
check_include_file_cxx("endian.h"      HAVE_ENDIAN_H)
check_include_file_cxx("sys/endian.h"  HAVE_SYS_ENDIAN_H)
check_include_file_cxx("execinfo.h"    HAVE_EXECINFO_H)

cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(htole32 "endian.h" HAVE_HTOLE32)
check_symbol_exists(be64toh "endian.h" HAVE_BE64TOH)
cmake_pop_check_state()

include(TestBigEndian)
test_big_endian(EMULE_BIG_ENDIAN)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/config.h"
    @ONLY
)

# Make generated config.h available to all targets
add_library(emule_generated_config INTERFACE)
target_include_directories(emule_generated_config INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

# ---------------------------------------------------------------------------
# Third-party dependencies (optional — found when available)
# ---------------------------------------------------------------------------
if(WIN32 AND NOT DEFINED OPENSSL_ROOT_DIR)
    foreach(_ssl_dir "C:/Program Files/OpenSSL-Win64" "C:/OpenSSL-Win64"
                     "$ENV{USERPROFILE}/scoop/apps/openssl/current")
        if(EXISTS "${_ssl_dir}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "${_ssl_dir}" CACHE PATH "OpenSSL root")
            break()
        endif()
    endforeach()
endif()

# Homebrew upgrades delete the versioned Cellar dir the cache pinned (e.g.
# openssl@3/3.6.1) — drop stale entries and prefer the stable opt/ symlink
if(OPENSSL_CRYPTO_LIBRARY AND NOT EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
    unset(OPENSSL_CRYPTO_LIBRARY CACHE)
    unset(OPENSSL_SSL_LIBRARY CACHE)
    unset(OPENSSL_INCLUDE_DIR CACHE)
    unset(pkgcfg_lib__OPENSSL_crypto CACHE)
    unset(pkgcfg_lib__OPENSSL_ssl CACHE)
endif()
if(APPLE AND NOT DEFINED OPENSSL_ROOT_DIR)
    foreach(_ssl_dir "/opt/homebrew/opt/openssl@3" "/usr/local/opt/openssl@3")
        if(EXISTS "${_ssl_dir}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "${_ssl_dir}" CACHE PATH "OpenSSL root")
            break()
        endif()
    endforeach()
endif()

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

# ---------------------------------------------------------------------------
# Allow older cmake_minimum_required() in third-party FetchContent deps
# (libarchive 3.7.7 requires CMake 3.1, yaml-cpp 0.8 requires CMake 3.4)
# ---------------------------------------------------------------------------
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# ---------------------------------------------------------------------------
# miniupnpc — UPnP port mapping (Module 6)
# ---------------------------------------------------------------------------
include(FetchContent)
find_package(miniupnpc CONFIG QUIET)
if(miniupnpc_FOUND)
    # vcpkg target is miniupnpc::miniupnpc; create alias for FetchContent name
    if(NOT TARGET libminiupnpc-static)
        add_library(libminiupnpc-static ALIAS miniupnpc::miniupnpc)
    endif()
else()
    FetchContent_Declare(miniupnpc
        GIT_REPOSITORY https://github.com/miniupnp/miniupnp.git
        GIT_TAG        miniupnpc_2_3_0
        SOURCE_SUBDIR  miniupnpc
    )
    set(UPNPC_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_SAMPLE  OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(miniupnpc)
endif()

# ---------------------------------------------------------------------------
# yaml-cpp — YAML persistence for preferences (Module 16)
# ---------------------------------------------------------------------------
find_package(yaml-cpp CONFIG QUIET)
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0
    )
    set(YAML_CPP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_CONTRIB  OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
    # FetchContent creates "yaml-cpp" target; add namespaced alias so
    # target_link_libraries(... yaml-cpp::yaml-cpp) works uniformly.
    if(TARGET yaml-cpp AND NOT TARGET yaml-cpp::yaml-cpp)
        add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
    endif()
endif()

# Suppress warnings from yaml-cpp headers
if(TARGET yaml-cpp)
    get_target_property(_yamlcpp_includes yaml-cpp INTERFACE_INCLUDE_DIRECTORIES)
    if(_yamlcpp_includes)
        set_target_properties(yaml-cpp PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_yamlcpp_includes}"
        )
    endif()
elseif(TARGET yaml-cpp::yaml-cpp)
    get_target_property(_yamlcpp_includes yaml-cpp::yaml-cpp INTERFACE_INCLUDE_DIRECTORIES)
    if(_yamlcpp_includes)
        set_target_properties(yaml-cpp::yaml-cpp PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_yamlcpp_includes}"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# libarchive — unified archive reading (Module 11)
# ---------------------------------------------------------------------------
find_package(LibArchive QUIET)
if(LibArchive_FOUND)
    # vcpkg/system provides libarchive — create alias matching FetchContent target name
    if(NOT TARGET archive)
        add_library(archive ALIAS LibArchive::LibArchive)
    endif()
    if(NOT TARGET archive_static)
        # GUI/core link against archive_static; point it to the shared lib
        add_library(archive_static ALIAS LibArchive::LibArchive)
    endif()
else()
    FetchContent_Declare(libarchive
        GIT_REPOSITORY https://github.com/libarchive/libarchive.git
        GIT_TAG        v3.7.7
    )
    set(ENABLE_TEST         OFF CACHE BOOL "" FORCE)
    set(ENABLE_INSTALL      OFF CACHE BOOL "" FORCE)
    set(ENABLE_TAR          OFF CACHE BOOL "" FORCE)
    set(ENABLE_CPIO         OFF CACHE BOOL "" FORCE)
    set(ENABLE_CAT          OFF CACHE BOOL "" FORCE)
    set(ENABLE_UNZIP        OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIBXML2      OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXPAT        OFF CACHE BOOL "" FORCE)
    set(ENABLE_PCRE2POSIX   OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libarchive)
    # archive_static sets LIBARCHIVE_STATIC only PRIVATE; consumers on Windows
    # need it too so the headers don't use __declspec(dllimport).
    if(WIN32 AND TARGET archive_static)
        target_compile_definitions(archive_static PUBLIC LIBARCHIVE_STATIC)
    endif()
endif()

# ---------------------------------------------------------------------------
# rapidyenc — SIMD yEnc decode + CRC32 for the Usenet module (Module: usenet)
#
# Public domain / CC0, so no licence obligations on a GPL-2.0 codebase. Runtime
# SIMD dispatch (SSE2 through AVX512, NEON, RVV) means no per-file -m flags in
# our build.
#
# Optional on purpose. src/usenet/decode/YencDecoder carries a scalar decoder
# that is correct, tested, and fast enough for a single connection; rapidyenc
# only earns its keep once the connection pool is saturating a link. Keeping the
# switch means a fetch or toolchain problem here can never block the module.
# ---------------------------------------------------------------------------
option(EMULE_USENET_RAPIDYENC "Use rapidyenc for SIMD yEnc decoding" ON)

if(EMULE_USENET_RAPIDYENC)
    # A prebuilt copy wins. The vcpkg overlay port in src/vcpkg-ports/ installs
    # one, which is what lets the hand-maintained MSBuild solution link rapidyenc
    # at all and stops the Windows CI job rebuilding it from source every run.
    # Under the vcpkg toolchain these two searches hit vcpkg_installed on their
    # own; everywhere else they miss and we build it ourselves below.
    find_path(RAPIDYENC_INCLUDE_DIR rapidyenc.h)
    find_library(RAPIDYENC_LIBRARY NAMES rapidyenc rapidyenc_static)

    if(RAPIDYENC_INCLUDE_DIR AND RAPIDYENC_LIBRARY)
        if(NOT TARGET rapidyenc_static)
            add_library(rapidyenc_static STATIC IMPORTED GLOBAL)
            set_target_properties(rapidyenc_static PROPERTIES
                IMPORTED_LOCATION "${RAPIDYENC_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${RAPIDYENC_INCLUDE_DIR}")
        endif()
        message(STATUS "rapidyenc: prebuilt ${RAPIDYENC_LIBRARY}")
    else()
        FetchContent_Declare(rapidyenc
            GIT_REPOSITORY https://github.com/animetosho/rapidyenc.git
            GIT_TAG        v1.1.1
        )
        # We decode; we never post. Dropping the encoder and the CLI is most of the
        # build.
        set(DISABLE_ENCODE ON  CACHE BOOL "" FORCE)
        set(DISABLE_DECODE OFF CACHE BOOL "" FORCE)
        set(DISABLE_CRC    OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(rapidyenc)

        if(TARGET rapidyenc_static)
            # Its own tool targets are of no use to us and would only be built.
            foreach(_ry_tool rapidyenc_cli rapidyenc_bench)
                if(TARGET ${_ry_tool})
                    set_target_properties(${_ry_tool} PROPERTIES EXCLUDE_FROM_ALL TRUE)
                endif()
            endforeach()
            target_include_directories(rapidyenc_static
                INTERFACE "${rapidyenc_SOURCE_DIR}")
        else()
            message(WARNING "rapidyenc target not found - falling back to the scalar decoder")
            set(EMULE_USENET_RAPIDYENC OFF)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# libpar2-turbo - PAR2 verify and repair for the Usenet module (phase 4)
#
# GPL-2.0, the same licence as eMuleQt, so nothing to reconcile.
#
# This is the *nzbgetcom fork*, not animetosho's original. The fork is what
# carries BUILD_LIB / BUILD_TOOL and an include/par2 tree; upstream builds only
# a CLI and cannot be consumed as a library. NZBGet ships exactly this.
#
# Optional for the same reason rapidyenc is: without it the queue still
# downloads, assembles and shares. It simply cannot verify or repair, and says
# so rather than pretending a damaged release is fine.
# ---------------------------------------------------------------------------
option(EMULE_USENET_PAR2 "Use libpar2-turbo for PAR2 verify and repair" ON)

# par2cmdline-turbo sets these with add_compile_definitions(), which is directory
# scope and therefore does NOT reach a consumer. They are not optional:
#
#   HAVE_CONFIG_H              libpar2.h picks its u32/u64 typedefs from config.h
#                              when this is set and from plain built-ins when it
#                              is not. On LP64 that is uint64_t vs unsigned long
#                              long -- same size, *different mangled name*, so a
#                              mismatch is an undefined reference to Process().
#   PARPAR_ENABLE_HASHER_MD5CRC   without it md5.h and crc.h declare nothing and
#                              filechecksummer.h fails on unknown type MD5Single
#   PARPAR_INVERT_SUPPORT / PARPAR_SLIM_GF16   gf16 header layout
#
# The include path must therefore also carry the config.h the library was built
# against -- the FetchContent branch adds its build dir, the vcpkg port installs
# a copy beside the headers.
set(EMULE_PAR2_DEFINES
    HAVE_CONFIG_H
    PARPAR_ENABLE_HASHER_MD5CRC
    PARPAR_INVERT_SUPPORT
    PARPAR_SLIM_GF16)

if(EMULE_USENET_PAR2)
    find_path(PAR2_INCLUDE_DIR par2/par2repairer.h)
    find_library(PAR2_TURBO_LIBRARY  par2-turbo)
    find_library(PAR2_GF16_LIBRARY   gf16)
    find_library(PAR2_HASHER_LIBRARY hasher)

    if(PAR2_INCLUDE_DIR AND PAR2_TURBO_LIBRARY AND PAR2_GF16_LIBRARY AND PAR2_HASHER_LIBRARY)
        add_library(emule_par2 INTERFACE)
        # SYSTEM: par2cmdline-turbo is warning-noisy and none of it is ours to fix.
        target_include_directories(emule_par2 SYSTEM INTERFACE "${PAR2_INCLUDE_DIR}")
        target_link_libraries(emule_par2 INTERFACE
            "${PAR2_TURBO_LIBRARY}" "${PAR2_GF16_LIBRARY}" "${PAR2_HASHER_LIBRARY}")
        target_compile_definitions(emule_par2 INTERFACE ${EMULE_PAR2_DEFINES})
        message(STATUS "par2: prebuilt ${PAR2_TURBO_LIBRARY}")
    else()
        FetchContent_Declare(par2turbo
            GIT_REPOSITORY https://github.com/nzbgetcom/par2cmdline-turbo.git
            GIT_TAG        v1.4.0-20260803
        )
        set(BUILD_LIB  ON  CACHE BOOL "" FORCE)   # its own default; stated anyway
        set(BUILD_TOOL OFF CACHE BOOL "" FORCE)   # we never invoke the par2 CLI
        FetchContent_MakeAvailable(par2turbo)

        if(TARGET par2-turbo)
            add_library(emule_par2 INTERFACE)
            target_link_libraries(emule_par2 INTERFACE par2-turbo)
            # gf16 and hasher come from include()d cmake files rather than
            # add_subdirectory, so they land in this scope under their bare names.
            foreach(_p2_lib gf16 hasher)
                if(TARGET ${_p2_lib})
                    target_link_libraries(emule_par2 INTERFACE ${_p2_lib})
                endif()
            endforeach()
            target_include_directories(emule_par2 SYSTEM INTERFACE
                "${par2turbo_SOURCE_DIR}/include"
                "${par2turbo_BINARY_DIR}")          # its generated config.h
            target_compile_definitions(emule_par2 INTERFACE ${EMULE_PAR2_DEFINES})

            # par2cmdline-turbo builds itself as a standalone CLI: its
            # cmake/common.cmake does add_compile_options(/MT) in Release, so its
            # objects want the static CRT while everything else here uses the DLL
            # one -- LNK2038 on every par2/gf16/hasher object. add_compile_options
            # is directory scope, which CMake copies into each target's
            # COMPILE_OPTIONS, so strip it there. The trailing /MD is what cl obeys
            # (last flag wins); the property only bites if that directory honours
            # CMP0091, and it declares cmake_minimum_required(3.13), so it does
            # not. C/CXX only: gf16 has a MASM source and ml64 knows no /MD.
            # The vcpkg path -- what CI builds -- patches the flag out instead;
            # see src/vcpkg-ports/par2-turbo/fix-msvc-crt.patch.
            if(MSVC)
                foreach(_p2_lib par2-turbo gf16 hasher)
                    if(NOT TARGET ${_p2_lib})
                        continue()
                    endif()
                    get_target_property(_p2_opts ${_p2_lib} COMPILE_OPTIONS)
                    if(NOT _p2_opts)
                        set(_p2_opts "")
                    endif()
                    list(REMOVE_ITEM _p2_opts /MT /MTd)
                    list(APPEND _p2_opts
                        "$<$<COMPILE_LANGUAGE:C,CXX>:$<IF:$<CONFIG:Debug>,/MDd,/MD>>")
                    set_property(TARGET ${_p2_lib} PROPERTY COMPILE_OPTIONS "${_p2_opts}")
                    set_property(TARGET ${_p2_lib} PROPERTY MSVC_RUNTIME_LIBRARY
                        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
                endforeach()
            endif()
        else()
            message(WARNING "par2-turbo target not found - PAR2 verify/repair disabled")
            set(EMULE_USENET_PAR2 OFF)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# libmaxminddb — GeoLite2 country lookup for the client/server flags
#
# Apache-2.0. Windows takes the vcpkg build; elsewhere we always build it static
# so a Homebrew/distro dylib can never end up as a new bundle dependency.
# ---------------------------------------------------------------------------
if(WIN32)
    find_package(maxminddb CONFIG QUIET)
endif()
if(NOT TARGET maxminddb::maxminddb)
    FetchContent_Declare(maxminddb
        GIT_REPOSITORY https://github.com/maxmind/libmaxminddb.git
        GIT_TAG        1.14.1
    )
    set(MAXMINDDB_BUILD_BINARIES OFF CACHE BOOL "" FORCE)
    set(MAXMINDDB_INSTALL        OFF CACHE BOOL "" FORCE)
    set(_emule_saved_build_testing ${BUILD_TESTING})
    set(BUILD_TESTING OFF)
    set(_emule_saved_shared ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(maxminddb)
    set(BUILD_TESTING ${_emule_saved_build_testing})
    set(BUILD_SHARED_LIBS ${_emule_saved_shared})
    if(TARGET maxminddb AND NOT TARGET maxminddb::maxminddb)
        add_library(maxminddb::maxminddb ALIAS maxminddb)
    endif()
endif()

# ---------------------------------------------------------------------------
# specialdlp — anti-leecher rules (eMule Xtreme DLP), a shared library that
# emulecored loads at runtime so it can be updated on its own (DlpLoader).
# ---------------------------------------------------------------------------
option(EMULE_SPECIALDLP "Build and ship the specialdlp anti-leecher library" ON)
if(EMULE_SPECIALDLP)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/specialdlp/CMakeLists.txt")
        message(FATAL_ERROR "external/specialdlp is missing — run: git submodule update --init")
    endif()
    add_subdirectory(external/specialdlp)
endif()

# ---------------------------------------------------------------------------
# libtorrent-rasterbar — BitTorrent engine of src/bt (docs/BitTorrentModule-Research.local.md)
#
# An installed one first (vcpkg, Homebrew, distro), else a static build of v2.1.1
# against Boost headers and the OpenSSL found above (same libssl as ngtcp2/Qt).
# Link the exported target only: it carries TORRENT_ABI_VERSION, which must be the
# same in the library and in every unit that includes its headers.
#
# Boost: headers only (libtorrent 2.x links no Boost library). A config package
# (vcpkg, Homebrew, libboost-dev) or a bare boost/ tree under BOOST_ROOT or a
# CMAKE_PREFIX_PATH entry's include/ (scripts/build-{linux,macos}-deps.sh).
# ---------------------------------------------------------------------------
option(EMULE_BITTORRENT "Build the BitTorrent module (libtorrent-rasterbar)" ON)
if(EMULE_BITTORRENT)
    set(_emule_bt_hint "Install Boost >= 1.74 headers (brew install boost, apt install libboost-dev, \
vcpkg, or set BOOST_ROOT) or libtorrent-rasterbar >= 2.1, or pass -DEMULE_BITTORRENT=OFF")
    # 2.1: pread_disk_io, torrent_info::layout() (src/bt/session/BtJoinDiskIo.cpp)
    find_package(LibtorrentRasterbar 2.1 CONFIG QUIET)
    if(LibtorrentRasterbar_FOUND)
        message(STATUS "libtorrent-rasterbar ${LibtorrentRasterbar_VERSION}: installed")
        # Release lib, but its config adds TORRENT_USE_ASSERTS in Debug: other class layout, crash
        get_target_property(_lt_real LibtorrentRasterbar::torrent-rasterbar ALIASED_TARGET)
        if(NOT _lt_real)
            set(_lt_real LibtorrentRasterbar::torrent-rasterbar)
        endif()
        get_target_property(_lt_defs ${_lt_real} INTERFACE_COMPILE_DEFINITIONS)
        if(_lt_defs)
            list(FILTER _lt_defs EXCLUDE REGEX "TORRENT_USE_ASSERTS")
            set_target_properties(${_lt_real} PROPERTIES INTERFACE_COMPILE_DEFINITIONS "${_lt_defs}")
        endif()
    else()
        find_package(Boost 1.74 CONFIG QUIET)
        if(Boost_FOUND)
            message(STATUS "Boost ${Boost_VERSION}: ${Boost_DIR}")
        else()
            # bare header tree: no BoostConfig.cmake, so find it by hand
            find_path(EMULE_BOOST_INCLUDE_DIR boost/version.hpp
                HINTS ${BOOST_ROOT} ${Boost_ROOT} $ENV{BOOST_ROOT} $ENV{Boost_ROOT}
                PATH_SUFFIXES include)
            set(_emule_boost_ver 0)
            if(EMULE_BOOST_INCLUDE_DIR)
                file(STRINGS "${EMULE_BOOST_INCLUDE_DIR}/boost/version.hpp" _emule_boost_ver
                     REGEX "^#define BOOST_VERSION [0-9]+")
                string(REGEX REPLACE "^#define BOOST_VERSION ([0-9]+).*" "\\1" _emule_boost_ver "${_emule_boost_ver}")
            endif()
            if(_emule_boost_ver LESS 107400)
                unset(EMULE_BOOST_INCLUDE_DIR CACHE)
                message(FATAL_ERROR "BitTorrent: no Boost >= 1.74 found for libtorrent-rasterbar. ${_emule_bt_hint}")
            endif()
            math(EXPR _emule_boost_minor "${_emule_boost_ver} / 100 % 1000")
            message(STATUS "Boost 1.${_emule_boost_minor} headers: ${EMULE_BOOST_INCLUDE_DIR}")
            # libtorrent's find_package(Boost) is module mode (FindBoost): hand it the tree
            set(Boost_INCLUDE_DIR "${EMULE_BOOST_INCLUDE_DIR}" CACHE PATH "Boost headers" FORCE)
        endif()
        message(STATUS "libtorrent-rasterbar: FetchContent v2.1.1 (static)")
        # BUILD_SHARED_LIBS is global: off for libtorrent only. A normal variable:
        # it shadows any cache entry (CMP0126) and libtorrent's option() keeps it (CMP0077)
        set(_emule_saved_shared ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF)
        set(build_tests OFF CACHE BOOL "" FORCE)
        set(build_examples OFF CACHE BOOL "" FORCE)
        set(build_tools OFF CACHE BOOL "" FORCE)
        set(python-bindings OFF CACHE BOOL "" FORCE)
        # WebTorrent needs the libdatachannel submodule; not used
        set(webtorrent OFF CACHE BOOL "" FORCE)
        # TORRENT_ABI_VERSION=100: no deprecated API
        set(deprecated-functions OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(libtorrent
            GIT_REPOSITORY https://github.com/arvidn/libtorrent.git
            GIT_TAG        v2.1.1
            GIT_SHALLOW    TRUE
            # try_signal is in the library's source list; the other submodules are not needed
            GIT_SUBMODULES deps/try_signal
        )
        FetchContent_MakeAvailable(libtorrent)
        set(BUILD_SHARED_LIBS ${_emule_saved_shared})
        if(TARGET torrent-rasterbar AND NOT TARGET LibtorrentRasterbar::torrent-rasterbar)
            add_library(LibtorrentRasterbar::torrent-rasterbar ALIAS torrent-rasterbar)
        endif()
    endif()

    # libtorrent and Boost headers are not ours to warn about
    foreach(_lt_target LibtorrentRasterbar::torrent-rasterbar torrent-rasterbar)
        if(TARGET ${_lt_target})
            get_target_property(_lt_real ${_lt_target} ALIASED_TARGET)
            if(NOT _lt_real)
                set(_lt_real ${_lt_target})
            endif()
            get_target_property(_lt_includes ${_lt_real} INTERFACE_INCLUDE_DIRECTORIES)
            if(_lt_includes)
                set_target_properties(${_lt_real} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_lt_includes}")
            endif()
            break()
        endif()
    endforeach()
    if(NOT TARGET LibtorrentRasterbar::torrent-rasterbar)
        message(FATAL_ERROR "BitTorrent: libtorrent-rasterbar target missing. ${_emule_bt_hint}")
    endif()
endif()
# The one place EMULE_HAVE_BITTORRENT is set: every target below inherits it
if(EMULE_BITTORRENT)
    add_compile_definitions(EMULE_HAVE_BITTORRENT=1)
endif()

# ---------------------------------------------------------------------------
# Hoster resolver: JDownloader's plugins in a Java sidecar (docs/hoster.md).
# OFF leaves out the resolver, the Java runtime lookup and the Hoster window;
# ed3k still stores, passes on and looks up hoster links and answers NO_RESOLVER.
# The sidecar itself is built by scripts/build-hoster-resolver.sh.
# ---------------------------------------------------------------------------
option(EMULE_HOSTER_JD "Build the JDownloader hoster resolver (ed3k hoster links)" ON)
# The one place EMULE_HAVE_HOSTER_JD is set: every target below inherits it
if(EMULE_HOSTER_JD)
    add_compile_definitions(EMULE_HAVE_HOSTER_JD=1)
endif()

# ---------------------------------------------------------------------------
# QUIC NAT-T transport (LowID<->LowID over a punched UDP path)
# ---------------------------------------------------------------------------
option(EMULE_QUIC_NATT "Build the QUIC NAT-T transport (ngtcp2, OpenSSL >= 3.5)" ON)
if(EMULE_QUIC_NATT)
    include(${CMAKE_SOURCE_DIR}/cmake/Ngtcp2.cmake)
endif()
# The one place EMULE_HAVE_QUIC_NATT is set
if(EMULE_QUIC_NATT)
    add_compile_definitions(EMULE_HAVE_QUIC_NATT=1)
endif()

# ---------------------------------------------------------------------------
# ed3k network (packmule, Rust). libs/packmule is a separate repo (gitignored):
# its packmule-ffi CMake runs cargo and gives packmule::ffi. Cargo profile
# follows CMAKE_BUILD_TYPE (Debug -> debug, else release). See docs/ed3k-module.md
# ---------------------------------------------------------------------------
option(EMULE_ED3K "ed3k network (packmule)" ON)
if(EMULE_ED3K)
    set(_emule_packmule_ffi "${CMAKE_SOURCE_DIR}/libs/packmule/crates/packmule-ffi")
    find_program(EMULE_CARGO cargo)
    if(EXISTS "${_emule_packmule_ffi}/CMakeLists.txt" AND EMULE_CARGO)
        # PACKMULE_FFI_TESTS defaults off when not top level
        set(CARGO "${EMULE_CARGO}" CACHE FILEPATH "cargo for packmule-ffi")
        add_subdirectory("${_emule_packmule_ffi}" "${CMAKE_BINARY_DIR}/packmule-ffi")
        message(STATUS "ed3k: packmule-ffi from ${_emule_packmule_ffi}")
    else()
        message(STATUS "ed3k: libs/packmule or cargo missing, EMULE_ED3K turned off")
        set(EMULE_ED3K OFF CACHE BOOL "ed3k network (packmule)" FORCE)
    endif()
endif()
# The one place EMULE_HAVE_ED3K is set
if(EMULE_ED3K)
    add_compile_definitions(EMULE_HAVE_ED3K=1)
endif()

# ed3k is early alpha: CI builds lock the node off (env EMULE_ED3K_LOCKED=1, set in the
# GitHub workflows and the Docker image). ToDo: drop once ed3k is ready for live use
if(DEFINED ENV{EMULE_ED3K_LOCKED} AND NOT "$ENV{EMULE_ED3K_LOCKED}" STREQUAL "" AND NOT "$ENV{EMULE_ED3K_LOCKED}" STREQUAL "0")
    set(_emule_ed3k_locked_default ON)
else()
    set(_emule_ed3k_locked_default OFF)
endif()
option(EMULE_ED3K_LOCKED "ed3k node can't be enabled (early alpha)" ${_emule_ed3k_locked_default})
if(EMULE_ED3K_LOCKED)
    add_compile_definitions(EMULE_ED3K_LOCKED=1)
    message(STATUS "ed3k: node locked off (EMULE_ED3K_LOCKED)")
endif()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------
option(EMULE_BUILD_TESTS "Build unit tests" ON)
option(EMULE_LIVE_TESTS  "Build live network integration tests (requires internet)" OFF)
if(EMULE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
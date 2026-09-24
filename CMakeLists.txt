cmake_minimum_required(VERSION 3.25)

project(eMuleQt
    VERSION 0.5.3
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
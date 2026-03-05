cmake_minimum_required(VERSION 3.25)

project(eMuleQt
    VERSION 0.1.0
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
find_package(ZLIB)
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
FetchContent_Declare(miniupnpc
    GIT_REPOSITORY https://github.com/miniupnp/miniupnp.git
    GIT_TAG        miniupnpc_2_3_0
    SOURCE_SUBDIR  miniupnpc
)
set(UPNPC_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SAMPLE  OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(miniupnpc)

# ---------------------------------------------------------------------------
# yaml-cpp — YAML persistence for preferences (Module 16)
# ---------------------------------------------------------------------------
FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
)
set(YAML_CPP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB  OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)

# Suppress warnings from yaml-cpp headers
get_target_property(_yamlcpp_includes yaml-cpp INTERFACE_INCLUDE_DIRECTORIES)
if(_yamlcpp_includes)
    set_target_properties(yaml-cpp PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_yamlcpp_includes}"
    )
endif()

# ---------------------------------------------------------------------------
# libarchive — unified archive reading (Module 11)
# ---------------------------------------------------------------------------
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
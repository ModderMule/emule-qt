# ---------------------------------------------------------------------------
# libemulecore — core library (no GUI dependency)
#
# Links only against Qt6::Core and Qt6::Network.
# All GUI code lives in src/gui/.
# ---------------------------------------------------------------------------

# Collect sources per sub-module.  Each subdirectory will accumulate its own
# source files as they are ported.  For now the library is a thin skeleton
# with the utils/ placeholder so CMake has something to compile.

# -- Sub-module source lists ------------------------------------------------

# Module 2+3: Platform abstraction & utilities
file(GLOB_RECURSE UTILS_SOURCES   CONFIGURE_DEPENDS "utils/*.cpp"   "utils/*.h")

# Module 4: Cryptography & hashing
file(GLOB_RECURSE CRYPTO_SOURCES  CONFIGURE_DEPENDS "crypto/*.cpp"  "crypto/*.h")

# Module 5: Networking & sockets
file(GLOB_RECURSE NET_SOURCES     CONFIGURE_DEPENDS "net/*.cpp"     "net/*.h")

# Module 7: ED2K protocol & packets
file(GLOB_RECURSE PROTO_SOURCES   CONFIGURE_DEPENDS "protocol/*.cpp" "protocol/*.h")

# Module 8: Server management
file(GLOB_RECURSE SERVER_SOURCES  CONFIGURE_DEPENDS "server/*.cpp"  "server/*.h")

# Module 9: Client management & credits
file(GLOB_RECURSE CLIENT_SOURCES  CONFIGURE_DEPENDS "client/*.cpp"  "client/*.h")

# Module 10: Kademlia / DHT
file(GLOB_RECURSE KAD_SOURCES     CONFIGURE_DEPENDS "kademlia/*.cpp" "kademlia/*.h")

# Module 11: File management
file(GLOB_RECURSE FILES_SOURCES   CONFIGURE_DEPENDS "files/*.cpp"   "files/*.h")

# Module 12: Download queue & transfer engine
file(GLOB_RECURSE XFER_SOURCES    CONFIGURE_DEPENDS "transfer/*.cpp" "transfer/*.h")

# Module 13: Search engine
file(GLOB_RECURSE SEARCH_SOURCES  CONFIGURE_DEPENDS "search/*.cpp"  "search/*.h")

# Module 14: IP filter & security
file(GLOB_RECURSE IPFILT_SOURCES  CONFIGURE_DEPENDS "ipfilter/*.cpp" "ipfilter/*.h")

# Module 15: Statistics & logging
file(GLOB_RECURSE STATS_SOURCES   CONFIGURE_DEPENDS "stats/*.cpp"   "stats/*.h")

# Module 16: Preferences & configuration
file(GLOB_RECURSE PREFS_SOURCES   CONFIGURE_DEPENDS "prefs/*.cpp"   "prefs/*.h")

# Module 17: Chat & IRC
file(GLOB_RECURSE CHAT_SOURCES    CONFIGURE_DEPENDS "chat/*.cpp"    "chat/*.h")

# Module 18: Friend system
file(GLOB_RECURSE FRIEND_SOURCES  CONFIGURE_DEPENDS "friends/*.cpp" "friends/*.h")

# Module 19: Web server
file(GLOB_RECURSE WEB_SOURCES     CONFIGURE_DEPENDS "webserver/*.cpp" "webserver/*.h")

# Module 6: Firewall & UPnP
file(GLOB_RECURSE UPNP_SOURCES    CONFIGURE_DEPENDS "upnp/*.cpp"    "upnp/*.h")

# Module 20: Media & preview
file(GLOB_RECURSE MEDIA_SOURCES   CONFIGURE_DEPENDS "media/*.cpp"   "media/*.h")

# Application context
file(GLOB_RECURSE APP_SOURCES     CONFIGURE_DEPENDS "app/*.cpp"     "app/*.h")

# Module 11: Archive support (ZIP/RAR/7z via libarchive)
file(GLOB_RECURSE ARCHIVE_SOURCES CONFIGURE_DEPENDS "archive/*.cpp" "archive/*.h")

# -- Library target ---------------------------------------------------------

add_library(emulecore STATIC
    ${UTILS_SOURCES}
    ${CRYPTO_SOURCES}
    ${NET_SOURCES}
    ${UPNP_SOURCES}
    ${PROTO_SOURCES}
    ${SERVER_SOURCES}
    ${CLIENT_SOURCES}
    ${KAD_SOURCES}
    ${FILES_SOURCES}
    ${XFER_SOURCES}
    ${SEARCH_SOURCES}
    ${IPFILT_SOURCES}
    ${STATS_SOURCES}
    ${PREFS_SOURCES}
    ${CHAT_SOURCES}
    ${FRIEND_SOURCES}
    ${WEB_SOURCES}
    ${MEDIA_SOURCES}
    ${ARCHIVE_SOURCES}
    ${APP_SOURCES}
)

add_library(eMule::Core ALIAS emulecore)

target_include_directories(emulecore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/utils"
)

target_link_libraries(emulecore
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::Multimedia
        Qt6::HttpServer
        eMule::CommonSettings
    PUBLIC
        ZLIB::ZLIB
        OpenSSL::Crypto
        libminiupnpc-static
        yaml-cpp::yaml-cpp
        archive_static
)

# -- Precompiled header (replaces old Stdafx.h) --------------------------------
# Stable headers that rarely change — speeds up incremental builds significantly.
target_precompile_headers(emulecore PRIVATE
    # C++ standard library
    <algorithm>
    <array>
    <chrono>
    <cmath>
    <cstdint>
    <cstring>
    <ctime>
    <deque>
    <functional>
    <memory>
    <mutex>
    <optional>
    <span>
    <string>
    <string_view>
    <unordered_map>
    <vector>
    # Qt Core (most frequently included across the codebase)
    <QByteArray>
    <QDebug>
    <QDir>
    <QFile>
    <QFileInfo>
    <QHostAddress>
    <QIODevice>
    <QList>
    <QMutex>
    <QObject>
    <QString>
    <QStringList>
    <QThread>
    <QTimer>
)

# Source groups for IDE navigation (mirrors directory structure)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${UTILS_SOURCES}  ${CRYPTO_SOURCES} ${NET_SOURCES}    ${UPNP_SOURCES}
    ${PROTO_SOURCES}   ${SERVER_SOURCES}  ${CLIENT_SOURCES} ${KAD_SOURCES}
    ${FILES_SOURCES}   ${XFER_SOURCES}    ${SEARCH_SOURCES} ${IPFILT_SOURCES}
    ${STATS_SOURCES}   ${PREFS_SOURCES}   ${CHAT_SOURCES}   ${FRIEND_SOURCES}
    ${WEB_SOURCES}     ${MEDIA_SOURCES}   ${ARCHIVE_SOURCES}
    ${APP_SOURCES}
)
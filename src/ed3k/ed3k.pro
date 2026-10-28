# emuleed3k — the ed3k node (packmule) static library
# Needs libs/packmule and cargo: packmule_ffi is built by cargo (QMAKE_PRE_LINK of the
# daemon is not used; run `cargo build --release -p packmule-ffi` in libs/packmule
# first, or build with CMake). See docs/ed3k-module.md.

TEMPLATE = lib
CONFIG  += staticlib c++2b
TARGET   = emuleed3k

QT += core network httpserver
QT -= gui

PACKMULE = $$PWD/../../libs/packmule
INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../core $$PWD/../usenet $$PWD/../bt $$PACKMULE/crates/packmule-ffi/include

DEFINES += EMULE_HAVE_ED3K=1
# BitTorrent: always on, as CMake EMULE_BITTORRENT (the torrent join links emulebt)
DEFINES += EMULE_HAVE_BITTORRENT=1 \
    TORRENT_USE_OPENSSL TORRENT_USE_LIBCRYPTO TORRENT_SSL_PEERS \
    BOOST_ASIO_NO_DEPRECATED BOOST_ASIO_ENABLE_CANCELIO BOOST_SYSTEM_USE_UTF8

unix: LIBS += -lcrypto
win32 {
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN
    OPENSSL_DIR = $$(OPENSSL_DIR)
    isEmpty(OPENSSL_DIR): OPENSSL_DIR = "C:/Program Files/OpenSSL-Win64"
    INCLUDEPATH += "$$OPENSSL_DIR/include"
    LIBS += -L"$$OPENSSL_DIR/lib" -lcrypto
}

SOURCES += \
    Ed3kBootstrap.cpp \
    Ed3kCredit.cpp \
    Ed3kDownloads.cpp \
    Ed3kSearch.cpp \
    Ed3kChain.cpp \
    Ed3kFilters.cpp \
    Ed3kUploads.cpp \
    Ed3kFriends.cpp \
    Ed3kRooms.cpp \
    Ed3kSets.cpp \
    Ed3kSetManifest.cpp \
    Ed3kLive.cpp \
    Ed3kLiveBroadcast.cpp \
    Ed3kPaid.cpp \
    Ed3kHosterJoin.cpp \
    Ed3kHttp.cpp \
    Ed3kJoinBase.cpp \
    Ed3kLan.cpp \
    Ed3kNames.cpp \
    Ed3kPlumbing.cpp \
    Ed3kSession.cpp \
    Ed3kShared.cpp \
    Ed3kSiteCache.cpp \
    Ed3kSiteServer.cpp \
    Ed3kStore.cpp \
    Ed3kTorrentJoin.cpp \
    Ed3kUsenetJoin.cpp

HEADERS += \
    Ed3kBootstrap.h \
    Ed3kCredit.h \
    Ed3kDownloads.h \
    Ed3kSearch.h \
    Ed3kChain.h \
    Ed3kFilters.h \
    Ed3kUploads.h \
    Ed3kFfi.h \
    Ed3kFriends.h \
    Ed3kRooms.h \
    Ed3kSets.h \
    Ed3kSetManifest.h \
    Ed3kLive.h \
    Ed3kLiveBroadcast.h \
    Ed3kPaid.h \
    Ed3kProto.h \
    Ed3kHosterJoin.h \
    Ed3kHttp.h \
    Ed3kJoinBase.h \
    Ed3kLan.h \
    Ed3kNames.h \
    Ed3kPlumbing.h \
    Ed3kSession.h \
    Ed3kShared.h \
    Ed3kSiteCache.h \
    Ed3kSiteServer.h \
    Ed3kStore.h \
    Ed3kTorrentJoin.h \
    Ed3kResolver.h \
    Ed3kUsenetJoin.h

# Hoster resolver: on by default, as CMake EMULE_HOSTER_JD. `qmake CONFIG+=no_emule_hoster_jd` turns it off
!no_emule_hoster_jd {
    DEFINES += EMULE_HAVE_HOSTER_JD=1
    SOURCES += Ed3kJavaRuntime.cpp Ed3kJdResolver.cpp
    HEADERS += Ed3kJavaRuntime.h Ed3kJdResolver.h
}

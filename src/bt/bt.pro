# emulebt — BitTorrent static library (libtorrent-rasterbar >= 2.1)
# CMake builds libtorrent itself when none is installed; qmake needs an installed one.

TEMPLATE = lib
CONFIG  += staticlib c++2b
TARGET   = emulebt

QT += core network
QT -= gui

INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../core

DEFINES += EMULE_HAVE_BITTORRENT=1 EMULE_HAVE_QUIC_NATT=1 \
    TORRENT_USE_OPENSSL TORRENT_USE_LIBCRYPTO TORRENT_SSL_PEERS \
    BOOST_ASIO_NO_DEPRECATED BOOST_ASIO_ENABLE_CANCELIO BOOST_SYSTEM_USE_UTF8

unix: LIBS += -ltorrent-rasterbar -lssl -lcrypto
win32 {
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN
    OPENSSL_DIR = $$(OPENSSL_DIR)
    isEmpty(OPENSSL_DIR): OPENSSL_DIR = "C:/Program Files/OpenSSL-Win64"
    INCLUDEPATH += "$$OPENSSL_DIR/include"
    LIBS += -L"$$OPENSSL_DIR/lib" -ltorrent-rasterbar -lssl -lcrypto
}

SOURCES += \
    BtIds.cpp \
    BtSession.cpp \
    add/BtAddSource.cpp \
    session/BtJoinDiskIo.cpp \
    session/BtSettingsMap.cpp \
    torrent/BtTorrentView.cpp

HEADERS += \
    BtIds.h \
    BtJoin.h \
    BtSession.h \
    BtTypes.h \
    add/BtAddSource.h \
    session/BtJoinDiskIo.h \
    session/BtSettingsMap.h \
    torrent/BtTorrentView.h

# emuleed3k — the ed3k node (packmule) static library
# Needs libs/packmule and cargo: packmule_ffi is built by cargo (QMAKE_PRE_LINK of the
# daemon is not used; run `cargo build --release -p packmule-ffi` in libs/packmule
# first, or build with CMake). See docs/ed3k-module.md.

TEMPLATE = lib
CONFIG  += staticlib c++2b
TARGET   = emuleed3k

QT += core network
QT -= gui

PACKMULE = $$PWD/../../libs/packmule
INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../core $$PWD/../usenet $$PACKMULE/crates/packmule-ffi/include

DEFINES += EMULE_HAVE_ED3K=1

unix: LIBS += -lcrypto
win32 {
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN
    OPENSSL_DIR = $$(OPENSSL_DIR)
    isEmpty(OPENSSL_DIR): OPENSSL_DIR = "C:/Program Files/OpenSSL-Win64"
    INCLUDEPATH += "$$OPENSSL_DIR/include"
    LIBS += -L"$$OPENSSL_DIR/lib" -lcrypto
}

SOURCES += \
    Ed3kHttp.cpp \
    Ed3kSession.cpp \
    Ed3kStore.cpp \
    Ed3kUsenetJoin.cpp

HEADERS += \
    Ed3kFfi.h \
    Ed3kHttp.h \
    Ed3kSession.h \
    Ed3kStore.h \
    Ed3kUsenetJoin.h

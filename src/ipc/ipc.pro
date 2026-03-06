# emuleipc — IPC static library

TEMPLATE = lib
CONFIG  += staticlib c++2b
TARGET   = emuleipc

QT += core network
QT -= gui

INCLUDEPATH += $$PWD

# OpenSSL
unix:  LIBS += -lssl -lcrypto
win32 {
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN

    OPENSSL_DIR = $$(OPENSSL_DIR)
    isEmpty(OPENSSL_DIR): OPENSSL_DIR = "C:/Program Files/OpenSSL-Win64"
    INCLUDEPATH += "$$OPENSSL_DIR/include"
    LIBS += -L"$$OPENSSL_DIR/lib" -lssl -lcrypto
}

SOURCES += \
    IpcConnection.cpp \
    IpcMessage.cpp \
    IpcProtocol.cpp

HEADERS += \
    CborSerializers.h \
    IpcConnection.h \
    IpcMessage.h \
    IpcProtocol.h

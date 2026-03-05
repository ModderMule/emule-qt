# emuleipc — IPC static library

TEMPLATE = lib
CONFIG  += staticlib c++2b
TARGET   = emuleipc

QT += core network
QT -= gui

INCLUDEPATH += $$PWD

# OpenSSL
unix:  LIBS += -lssl -lcrypto
win32: LIBS += -lssl -lcrypto

SOURCES += \
    IpcConnection.cpp \
    IpcMessage.cpp \
    IpcProtocol.cpp

HEADERS += \
    CborSerializers.h \
    IpcConnection.h \
    IpcMessage.h \
    IpcProtocol.h

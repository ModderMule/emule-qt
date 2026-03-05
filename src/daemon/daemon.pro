# emulecored — Headless daemon executable

TEMPLATE = app
CONFIG  += console c++2b
CONFIG  -= app_bundle
TARGET   = emulecored

QT += core network multimedia httpserver
QT -= gui

INCLUDEPATH += \
    $$PWD/../core \
    $$PWD/../ipc \
    $$PWD/../../build/generated

LIBS += \
    -L$$OUT_PWD/../core -lemulecore \
    -L$$OUT_PWD/../ipc  -lemuleipc

# Third-party libraries (system-installed or from CMake build)
unix {
    LIBS += -lssl -lcrypto -lz -lminiupnpc -lyaml-cpp -larchive
}
macx {
    LIBS += -framework IOKit -framework CoreFoundation -framework CoreServices
}
win32 {
    LIBS += -lssl -lcrypto -lz -lminiupnpc -lyaml-cpp -larchive -lws2_32 -liphlpapi
}

SOURCES += \
    CliIpcClient.cpp \
    CommandLineExec.cpp \
    CoreNotifierBridge.cpp \
    DaemonApp.cpp \
    IpcClientHandler.cpp \
    IpcServer.cpp \
    main.cpp

HEADERS += \
    CliIpcClient.h \
    CommandLineExec.h \
    CoreNotifierBridge.h \
    DaemonApp.h \
    IpcClientHandler.h \
    IpcServer.h

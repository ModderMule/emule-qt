# emulecored — Headless daemon executable

TEMPLATE = app
CONFIG  += console c++2b
CONFIG  -= app_bundle
TARGET   = emulecored

QT += core network multimedia httpserver
QT -= gui

INCLUDEPATH += \
    $$PWD/../core \
    $$PWD/../ipc

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
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN

    OPENSSL_DIR = $$(OPENSSL_DIR)
    isEmpty(OPENSSL_DIR): OPENSSL_DIR = "C:/Program Files/OpenSSL-Win64"

    ZLIB_DIR = $$(ZLIB_DIR)
    isEmpty(ZLIB_DIR): ZLIB_DIR = "C:/Program Files/zlib"

    MINIUPNPC_DIR = $$(MINIUPNPC_DIR)
    isEmpty(MINIUPNPC_DIR): MINIUPNPC_DIR = "C:/Program Files/miniupnpc"

    YAMLCPP_DIR = $$(YAMLCPP_DIR)
    isEmpty(YAMLCPP_DIR): YAMLCPP_DIR = "C:/Program Files/yaml-cpp"

    LIBARCHIVE_DIR = $$(LIBARCHIVE_DIR)
    isEmpty(LIBARCHIVE_DIR): LIBARCHIVE_DIR = "C:/Program Files/libarchive"

    INCLUDEPATH += \
        "$$OPENSSL_DIR/include" "$$ZLIB_DIR/include" \
        "$$MINIUPNPC_DIR/include" "$$YAMLCPP_DIR/include" "$$LIBARCHIVE_DIR/include"

    LIBS += \
        -L"$$OPENSSL_DIR/lib" -lssl -lcrypto \
        -L"$$ZLIB_DIR/lib" -lz \
        -L"$$MINIUPNPC_DIR/lib" -lminiupnpc \
        -L"$$YAMLCPP_DIR/lib" -lyaml-cpp \
        -L"$$LIBARCHIVE_DIR/lib" -larchive \
        -lws2_32 -liphlpapi
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

# emuleqt — GUI application

TEMPLATE = app
CONFIG  += c++2b
TARGET   = emuleqt

QT += core gui widgets network multimedia

INCLUDEPATH += \
    $$PWD/../core \
    $$PWD/../ipc

LIBS += \
    -L$$OUT_PWD/../core -lemulecore \
    -L$$OUT_PWD/../ipc  -lemuleipc

# Third-party libraries
unix {
    LIBS += -lssl -lcrypto -lz -lminiupnpc -lyaml-cpp -larchive
}
macx {
    LIBS += -framework IOKit -framework CoreFoundation -framework CoreServices
    ICON = ../../resources/icons/eMule.icns
    QMAKE_INFO_PLIST = Info.plist.in
}
linux {
    QT += dbus
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

RESOURCES += ../../resources/emuleqt.qrc

TRANSLATIONS += \
    ../../lang/emuleqt_en.ts \
    ../../lang/emuleqt_de_DE.ts \
    ../../lang/emuleqt_es_ES.ts \
    ../../lang/emuleqt_fr_FR.ts \
    ../../lang/emuleqt_it_IT.ts \
    ../../lang/emuleqt_ja_JP.ts \
    ../../lang/emuleqt_ko_KR.ts \
    ../../lang/emuleqt_pt_BR.ts \
    ../../lang/emuleqt_zh_CN.ts

SOURCES += \
    app/AutoStart.cpp \
    app/CommandLineExec.cpp \
    app/Ed2kSchemeHandler.cpp \
    app/IpcClient.cpp \
    app/main.cpp \
    app/MainWindow.cpp \
    app/MiniMuleWidget.cpp \
    app/PowerManager.cpp \
    app/UiState.cpp \
    app/VersionChecker.cpp \
    controls/ClientListModel.cpp \
    controls/ContactsGraph.cpp \
    controls/DownloadListModel.cpp \
    controls/DownloadProgressDelegate.cpp \
    controls/FriendListModel.cpp \
    controls/KadContactHistogram.cpp \
    controls/KadContactsModel.cpp \
    controls/KadLookupGraph.cpp \
    controls/KadSearchesModel.cpp \
    controls/LogWidget.cpp \
    controls/SearchResultsModel.cpp \
    controls/ServerListModel.cpp \
    controls/SharedFilesModel.cpp \
    controls/SharedPartsDelegate.cpp \
    controls/StatsGraph.cpp \
    controls/TransferToolbar.cpp \
    dialogs/AddFriendDialog.cpp \
    dialogs/ArchivePreviewPanel.cpp \
    dialogs/ClientDetailDialog.cpp \
    dialogs/CoreConnectDialog.cpp \
    dialogs/FileDetailDialog.cpp \
    dialogs/FirstStartWizard.cpp \
    dialogs/ImportDownloadsDialog.cpp \
    dialogs/NetworkInfoDialog.cpp \
    dialogs/OptionsDialog.cpp \
    dialogs/PasteLinksDialog.cpp \
    panels/IrcPanel.cpp \
    panels/KadPanel.cpp \
    panels/MessagesPanel.cpp \
    panels/SearchPanel.cpp \
    panels/ServerPanel.cpp \
    panels/SharedFilesPanel.cpp \
    panels/StatisticsPanel.cpp \
    panels/TransferPanel.cpp

HEADERS += \
    app/AutoStart.h \
    app/CommandLineExec.h \
    app/Ed2kSchemeHandler.h \
    app/IpcClient.h \
    app/MainWindow.h \
    app/MiniMuleWidget.h \
    app/PowerManager.h \
    app/UiState.h \
    app/VersionChecker.h \
    controls/ClientListModel.h \
    controls/ContactsGraph.h \
    controls/DownloadListModel.h \
    controls/DownloadProgressDelegate.h \
    controls/FriendListModel.h \
    controls/KadContactHistogram.h \
    controls/KadContactsModel.h \
    controls/KadLookupGraph.h \
    controls/KadSearchesModel.h \
    controls/LogWidget.h \
    controls/SearchResultsModel.h \
    controls/ServerListModel.h \
    controls/SharedFilesModel.h \
    controls/SharedPartsDelegate.h \
    controls/StatsGraph.h \
    controls/TransferToolbar.h \
    dialogs/AddFriendDialog.h \
    dialogs/ArchivePreviewPanel.h \
    dialogs/ClientDetailDialog.h \
    dialogs/CoreConnectDialog.h \
    dialogs/FileDetailDialog.h \
    dialogs/FirstStartWizard.h \
    dialogs/ImportDownloadsDialog.h \
    dialogs/NetworkInfoDialog.h \
    dialogs/OptionsDialog.h \
    dialogs/PasteLinksDialog.h \
    panels/IrcPanel.h \
    panels/KadPanel.h \
    panels/MessagesPanel.h \
    panels/SearchPanel.h \
    panels/ServerPanel.h \
    panels/SharedFilesPanel.h \
    panels/StatisticsPanel.h \
    panels/TransferPanel.h

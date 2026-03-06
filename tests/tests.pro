# eMuleQt tests — Single test executable with all test cases

TEMPLATE = app
CONFIG  += console testcase c++2b
TARGET   = emuleqt_tests

QT += core network multimedia httpserver testlib
QT -= gui

INCLUDEPATH += \
    $$PWD/../src/core \
    $$PWD/../src/ipc \
    $$PWD

LIBS += \
    -L$$OUT_PWD/../src/core -lemulecore \
    -L$$OUT_PWD/../src/ipc  -lemuleipc

# Third-party libraries
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
        -L"$$ZLIB_DIR/lib" -lzlib \
        -L"$$MINIUPNPC_DIR/lib" -lminiupnpc \
        -L"$$YAMLCPP_DIR/lib" -lyaml-cpp \
        -L"$$LIBARCHIVE_DIR/lib" -larchive \
        -lws2_32 -liphlpapi
}

# Test data paths
DEFINES += \
    EMULE_TEST_DATA_DIR=\\\"$$PWD/data/\\\" \
    EMULE_PROJECT_DATA_DIR=\\\"$$PWD/../data/\\\"

HEADERS += \
    TestHelpers.h

SOURCES += \
    tst_AbstractFile.cpp \
    tst_AICHHashSet.cpp \
    tst_AICHHashTree.cpp \
    tst_ArchiveReader.cpp \
    tst_ArchiveRecovery.cpp \
    tst_AtomicOps.cpp \
    tst_ClientCredits.cpp \
    tst_ClientList.cpp \
    tst_ClientStateDefs.cpp \
    tst_ClientUDPSocket.cpp \
    tst_CollectionFile.cpp \
    tst_CorruptionBlackBox.cpp \
    tst_DeadSourceList.cpp \
    tst_DownloadQueue.cpp \
    tst_ED2KLink.cpp \
    tst_EMSocket.cpp \
    tst_EncryptedDatagram.cpp \
    tst_FileDownloadLive.cpp \
    tst_FileIdentifier.cpp \
    tst_FrameGrabThread.cpp \
    tst_Friend.cpp \
    tst_FriendList.cpp \
    tst_HttpClientReqSocket.cpp \
    tst_ImportParts.cpp \
    tst_IPFilter.cpp \
    tst_IPFilterMatch.cpp \
    tst_IpcConnection.cpp \
    tst_IpcMessage.cpp \
    tst_IpcProtocol.cpp \
    tst_IrcClient.cpp \
    tst_IrcProtocol.cpp \
    tst_Kademlia.cpp \
    tst_KadEntry.cpp \
    tst_KadFirewallTester.cpp \
    tst_KadIndexed.cpp \
    tst_KadIO.cpp \
    tst_KadLookupHistory.cpp \
    tst_KadMiscUtils.cpp \
    tst_KadNodesData.cpp \
    tst_KadPacketTracking.cpp \
    tst_KadPrefs.cpp \
    tst_KadRoutingBin.cpp \
    tst_KadRoutingZone.cpp \
    tst_KadSearch.cpp \
    tst_KadSearchManager.cpp \
    tst_KadUDPListener.cpp \
    tst_KadUInt128.cpp \
    tst_KnownFile.cpp \
    tst_KnownFileList.cpp \
    tst_LastCommonRouteFinder.cpp \
    tst_ListenSocket.cpp \
    tst_Log.cpp \
    tst_MD4.cpp \
    tst_MD5.cpp \
    tst_MediaInfo.cpp \
    tst_OtherFunctions.cpp \
    tst_Packet.cpp \
    tst_PartFile.cpp \
    tst_PartFileConvert.cpp \
    tst_PartFileData.cpp \
    tst_PartFileWriteThread.cpp \
    tst_PathUtils.cpp \
    tst_Pinger.cpp \
    tst_PreviewApps.cpp \
    tst_PreviewThread.cpp \
    tst_SafeFile.cpp \
    tst_Scheduler.cpp \
    tst_SearchExpr.cpp \
    tst_SearchExprParser.cpp \
    tst_SearchFile.cpp \
    tst_SearchList.cpp \
    tst_SearchParams.cpp \
    tst_Server.cpp \
    tst_ServerDownloadLive.cpp \
    tst_ServerList.cpp \
    tst_ServerMetData.cpp \
    tst_ServerSocket.cpp \
    tst_SHA.cpp \
    tst_SharedFileData.cpp \
    tst_SharedFileList.cpp \
    tst_Smoke.cpp \
    tst_StatisticFile.cpp \
    tst_Statistics.cpp \
    tst_StatisticsReset.cpp \
    tst_StringConversion.cpp \
    tst_Tags.cpp \
    tst_TcpConnect.cpp \
    tst_TimeUtils.cpp \
    tst_TypeDefs.cpp \
    tst_UDPSocket.cpp \
    tst_UpDownClient.cpp \
    tst_UploadBandwidthThrottler.cpp \
    tst_UploadDiskIOThread.cpp \
    tst_UploadQueue.cpp \
    tst_UPnPManager.cpp \
    tst_URLClient.cpp \
    tst_WebServer.cpp

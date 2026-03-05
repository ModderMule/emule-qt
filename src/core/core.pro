# emulecore — Core static library

TEMPLATE = lib
CONFIG  += staticlib c++2b
TARGET   = emulecore

QT += core network multimedia httpserver concurrent
QT -= gui

DEFINES += QT_NO_CAST_FROM_ASCII

INCLUDEPATH += \
    $$PWD \
    $$PWD/../../build/generated

# OpenSSL & zlib
unix {
    LIBS += -lssl -lcrypto -lz
}
win32 {
    LIBS += -lssl -lcrypto -lz
}

# Third-party: miniupnpc, yaml-cpp, libarchive
# Prefer pkg-config when available; fall back to system paths.
unix:!macx {
    CONFIG += link_pkgconfig
    PKGCONFIG += miniupnpc yaml-cpp libarchive
}
macx {
    # Homebrew / system-installed
    LIBS += -lminiupnpc -lyaml-cpp -larchive
}
win32 {
    LIBS += -lminiupnpc -lyaml-cpp -larchive
}

# macOS frameworks
macx {
    LIBS += -framework IOKit -framework CoreFoundation -framework CoreServices
}

SOURCES += \
    app/AppConfig.cpp \
    app/AppContext.cpp \
    app/CoreSession.cpp \
    archive/ArchiveReader.cpp \
    archive/ArchiveRecovery.cpp \
    chat/IrcClient.cpp \
    client/ClientCredits.cpp \
    client/ClientList.cpp \
    client/CorruptionBlackBox.cpp \
    client/DeadSourceList.cpp \
    client/DownloadClient.cpp \
    client/UpDownClient.cpp \
    client/UploadClient.cpp \
    client/URLClient.cpp \
    crypto/AICHData.cpp \
    crypto/AICHHashSet.cpp \
    crypto/AICHHashTree.cpp \
    crypto/AICHSyncThread.cpp \
    crypto/FileIdentifier.cpp \
    crypto/MD4Hash.cpp \
    crypto/MD5Hash.cpp \
    crypto/SHAHash.cpp \
    files/AbstractFile.cpp \
    files/CollectionFile.cpp \
    files/KnownFile.cpp \
    files/KnownFileList.cpp \
    files/PartFile.cpp \
    files/PartFileConvert.cpp \
    files/PartFileWriteThread.cpp \
    files/PublishKeywordList.cpp \
    files/ShareableFile.cpp \
    files/SharedFileList.cpp \
    files/StatisticFile.cpp \
    friends/Friend.cpp \
    friends/FriendList.cpp \
    ipfilter/IPFilter.cpp \
    kademlia/Kademlia.cpp \
    kademlia/KadContact.cpp \
    kademlia/KadEntry.cpp \
    kademlia/KadFirewallTester.cpp \
    kademlia/KadIndexed.cpp \
    kademlia/KadIO.cpp \
    kademlia/KadKeywordSpacePartitioner.cpp \
    kademlia/KadLog.cpp \
    kademlia/KadLookupHistory.cpp \
    kademlia/KadMiscUtils.cpp \
    kademlia/KadPacketTracking.cpp \
    kademlia/KadPrefs.cpp \
    kademlia/KadRoutingBin.cpp \
    kademlia/KadRoutingZone.cpp \
    kademlia/KadSearch.cpp \
    kademlia/KadSearchManager.cpp \
    kademlia/KadUDPListener.cpp \
    kademlia/KadUInt128.cpp \
    media/FrameGrabThread.cpp \
    media/MediaInfo.cpp \
    media/PreviewApps.cpp \
    media/PreviewThread.cpp \
    net/ClientReqSocket.cpp \
    net/ClientUDPSocket.cpp \
    net/EMSocket.cpp \
    net/EncryptedDatagramSocket.cpp \
    net/EncryptedStreamSocket.cpp \
    net/HttpClientReqSocket.cpp \
    net/LastCommonRouteFinder.cpp \
    net/ListenSocket.cpp \
    net/Packet.cpp \
    net/Pinger.cpp \
    net/ServerSocket.cpp \
    net/SmtpClient.cpp \
    net/UDPSocket.cpp \
    prefs/Preferences.cpp \
    protocol/ED2KLink.cpp \
    protocol/Tag.cpp \
    search/SearchExpr.cpp \
    search/SearchExprParser.cpp \
    search/SearchFile.cpp \
    search/SearchList.cpp \
    search/SearchParams.cpp \
    server/Server.cpp \
    server/ServerConnect.cpp \
    server/ServerList.cpp \
    stats/Statistics.cpp \
    transfer/DownloadQueue.cpp \
    transfer/ImportParts.cpp \
    transfer/Scheduler.cpp \
    transfer/UploadBandwidthThrottler.cpp \
    transfer/UploadDiskIOThread.cpp \
    transfer/UploadQueue.cpp \
    upnp/UPnPManager.cpp \
    utils/DebugUtils.cpp \
    utils/Log.cpp \
    utils/OtherFunctions.cpp \
    utils/PathUtils.cpp \
    utils/PerfLog.cpp \
    utils/SafeFile.cpp \
    utils/SettingsUtils.cpp \
    utils/StringUtils.cpp \
    webserver/WebServer.cpp \
    webserver/WebSessionManager.cpp \
    webserver/WebTemplateEngine.cpp

HEADERS += \
    app/AppConfig.h \
    app/AppContext.h \
    app/CoreSession.h \
    archive/ArchiveReader.h \
    archive/ArchiveRecovery.h \
    chat/IrcClient.h \
    chat/IrcMessage.h \
    client/ClientCredits.h \
    client/ClientList.h \
    client/ClientStateDefs.h \
    client/ClientStructs.h \
    client/CorruptionBlackBox.h \
    client/DeadSourceList.h \
    client/UpDownClient.h \
    client/URLClient.h \
    crypto/AICHData.h \
    crypto/AICHHashSet.h \
    crypto/AICHHashTree.h \
    crypto/AICHSyncThread.h \
    crypto/FileIdentifier.h \
    crypto/MD4Hash.h \
    crypto/MD5Hash.h \
    crypto/SHAHash.h \
    files/AbstractFile.h \
    files/CollectionFile.h \
    files/KnownFile.h \
    files/KnownFileList.h \
    files/PartFile.h \
    files/PartFileConvert.h \
    files/PartFileWriteThread.h \
    files/PublishKeywordList.h \
    files/ShareableFile.h \
    files/SharedFileList.h \
    files/StatisticFile.h \
    friends/Friend.h \
    friends/FriendList.h \
    ipfilter/IPFilter.h \
    kademlia/Kademlia.h \
    kademlia/KadClientSearcher.h \
    kademlia/KadContact.h \
    kademlia/KadEntry.h \
    kademlia/KadError.h \
    kademlia/KadFirewallTester.h \
    kademlia/KadIO.h \
    kademlia/KadIndexed.h \
    kademlia/KadLog.h \
    kademlia/KadLookupHistory.h \
    kademlia/KadMiscUtils.h \
    kademlia/KadPacketTracking.h \
    kademlia/KadPrefs.h \
    kademlia/KadRoutingBin.h \
    kademlia/KadRoutingZone.h \
    kademlia/KadSearch.h \
    kademlia/KadSearchDefs.h \
    kademlia/KadSearchManager.h \
    kademlia/KadTypes.h \
    kademlia/KadUDPKey.h \
    kademlia/KadUDPListener.h \
    kademlia/KadUInt128.h \
    media/FrameGrabThread.h \
    media/MediaInfo.h \
    media/PreviewApps.h \
    media/PreviewThread.h \
    net/ClientReqSocket.h \
    net/ClientUDPSocket.h \
    net/EMSocket.h \
    net/EncryptedDatagramSocket.h \
    net/EncryptedStreamSocket.h \
    net/HttpClientReqSocket.h \
    net/LastCommonRouteFinder.h \
    net/ListenSocket.h \
    net/Packet.h \
    net/Pinger.h \
    net/ServerSocket.h \
    net/SmtpClient.h \
    net/ThrottledSocket.h \
    net/UDPSocket.h \
    prefs/Preferences.h \
    protocol/ED2KLink.h \
    protocol/Tag.h \
    search/SearchExpr.h \
    search/SearchExprParser.h \
    search/SearchFile.h \
    search/SearchList.h \
    search/SearchParams.h \
    server/Server.h \
    server/ServerConnect.h \
    server/ServerList.h \
    stats/Statistics.h \
    transfer/DownloadQueue.h \
    transfer/ImportParts.h \
    transfer/Scheduler.h \
    transfer/UploadBandwidthThrottler.h \
    transfer/UploadDiskIOThread.h \
    transfer/UploadQueue.h \
    upnp/UPnPManager.h \
    utils/ByteOrder.h \
    utils/ContainerUtils.h \
    utils/DebugUtils.h \
    utils/Exceptions.h \
    utils/Log.h \
    utils/MapKey.h \
    utils/OtherFunctions.h \
    utils/PathUtils.h \
    utils/PerfLog.h \
    utils/PlatformUtils.h \
    utils/SafeFile.h \
    utils/SettingsUtils.h \
    utils/StringUtils.h \
    utils/ThreadUtils.h \
    utils/TimeUtils.h \
    utils/Types.h \
    utils/WinCompat.h \
    webserver/JsonSerializers.h \
    webserver/WebServer.h \
    webserver/WebSessionManager.h \
    webserver/WebTemplateEngine.h

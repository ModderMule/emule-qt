# ---------------------------------------------------------------------------
# eMule Qt — Unit Test Infrastructure
#
# Uses Qt 6 Test framework (QTest).
# Each tst_*.cpp file becomes a standalone test executable registered
# with CTest so that `ctest` or `cmake --build . --target test` runs them.
#
# See: https://doc.qt.io/qt-6/qtest-overview.html
# ---------------------------------------------------------------------------

find_package(Qt6 REQUIRED COMPONENTS Test)

# All tests are independent executables — safe to run in parallel:
#   ctest --parallel    (or set CTEST_PARALLEL_LEVEL env var)

# ---------------------------------------------------------------------------
# Shared test helpers (fixtures, mock factories, temp dir utilities)
# ---------------------------------------------------------------------------
add_library(emule_test_helpers INTERFACE)
add_library(eMule::TestHelpers ALIAS emule_test_helpers)

target_include_directories(emule_test_helpers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(emule_test_helpers INTERFACE
    Qt6::Test
    eMule::Core
    eMule::CommonSettings
)

# Test data directory — available to test code via EMULE_TEST_DATA_DIR define
# Project data directory — available via EMULE_PROJECT_DATA_DIR (server.met, nodes.dat)
target_compile_definitions(emule_test_helpers INTERFACE
    "EMULE_TEST_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data"
    "EMULE_PROJECT_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
)

# ---------------------------------------------------------------------------
# Helper function: add a Qt Test executable
#
# Usage:
#   emule_add_test(tst_MD4)
#
# This expects a file named tst_MD4.cpp in the current source directory,
# creates an executable tst_MD4, links it, and registers it with CTest.
# ---------------------------------------------------------------------------
function(emule_add_test TEST_NAME)
    set(options "")
    set(oneValueArgs FOLDER)
    set(multiValueArgs SOURCES LIBRARIES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Default source file: <TEST_NAME>.cpp
    if(NOT ARG_SOURCES)
        set(ARG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_NAME}.cpp")
    endif()

    qt_add_executable(${TEST_NAME} ${ARG_SOURCES})

    target_link_libraries(${TEST_NAME} PRIVATE
        eMule::TestHelpers
        ${ARG_LIBRARIES}
    )

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    # Ensure Qt platform plugin is found during CTest runs
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    if(ARG_FOLDER)
        set_target_properties(${TEST_NAME} PROPERTIES FOLDER "Tests/${ARG_FOLDER}")
    else()
        set_target_properties(${TEST_NAME} PROPERTIES FOLDER "Tests")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Register test suites — uncomment as tests are implemented
# ---------------------------------------------------------------------------

# Module 29: Smoke test to verify test infrastructure works
emule_add_test(tst_Smoke)

# Module 29a: Platform abstraction
emule_add_test(tst_TypeDefs         FOLDER "Platform")
emule_add_test(tst_StringConversion FOLDER "Platform")
emule_add_test(tst_PathUtils        FOLDER "Platform")
emule_add_test(tst_TimeUtils        FOLDER "Platform")
emule_add_test(tst_AtomicOps        FOLDER "Platform")

# Module 29b: Core utility functions
emule_add_test(tst_OtherFunctions  FOLDER "Utils")
emule_add_test(tst_SafeFile        FOLDER "Utils")
emule_add_test(tst_Log             FOLDER "Utils")
emule_add_test(tst_CorroborationTally FOLDER "Utils")

# Module 29c: Cryptography
emule_add_test(tst_AesCbc           FOLDER "Crypto")
emule_add_test(tst_MD4              FOLDER "Crypto")
emule_add_test(tst_MD5              FOLDER "Crypto")
emule_add_test(tst_SHA              FOLDER "Crypto")
emule_add_test(tst_AICHHashTree     FOLDER "Crypto")
emule_add_test(tst_AICHHashSet      FOLDER "Crypto")
emule_add_test(tst_FileIdentifier   FOLDER "Crypto")

# Module 29d: Networking
emule_add_test(tst_Address            FOLDER "Net")
emule_add_test(tst_LocalIPv6         FOLDER "Net")
emule_add_test(tst_Packet            FOLDER "Net")
emule_add_test(tst_EncryptedDatagram FOLDER "Net")
emule_add_test(tst_EMSocket          FOLDER "Net")
emule_add_test(tst_Pinger            FOLDER "Net")
emule_add_test(tst_HostResolver      FOLDER "Net")
emule_add_test(tst_DefaultGateway    FOLDER "Net")
emule_add_test(tst_PortMapWire       FOLDER "PortMap")
emule_add_test(tst_PortMapper        FOLDER "PortMap")
emule_add_test(tst_UdpMappingBackend FOLDER "PortMap")

# Module 5: Remaining networking classes
emule_add_test(tst_LastCommonRouteFinder FOLDER "Net")
emule_add_test(tst_ServerSocket          FOLDER "Net")
emule_add_test(tst_UDPSocket             FOLDER "Net")
emule_add_test(tst_ListenSocket          FOLDER "Net")
emule_add_test(tst_HttpClientReqSocket   FOLDER "Net")
emule_add_test(tst_HttpFileDownload      FOLDER "Net")
emule_add_test(tst_ClientUDPSocket       FOLDER "Net")
emule_add_test(tst_TcpConnect            FOLDER "Net")
set_tests_properties(tst_TcpConnect PROPERTIES TIMEOUT 30)

# Module 29e: Firewall & UPnP
emule_add_test(tst_UPnPBackend   FOLDER "PortMap")

# Module 29f: Protocol
emule_add_test(tst_Tags            FOLDER "Protocol")
emule_add_test(tst_HttpCacheOffer  FOLDER "Protocol")
emule_add_test(tst_HttpCacheResume FOLDER "Protocol")
set_tests_properties(tst_HttpCacheResume PROPERTIES TIMEOUT 120)
emule_add_test(tst_HttpCachePublish FOLDER "Protocol")
set_tests_properties(tst_HttpCachePublish PROPERTIES TIMEOUT 120)
emule_add_test(tst_HttpCacheMultiPeer FOLDER "Protocol")
target_compile_definitions(tst_HttpCacheMultiPeer PRIVATE QTEST_FUNCTION_TIMEOUT=120000)
set_tests_properties(tst_HttpCacheMultiPeer PROPERTIES TIMEOUT 180)
emule_add_test(tst_HttpCacheCorruptBan FOLDER "Protocol")
emule_add_test(tst_HttpCacheRelay FOLDER "Protocol")
target_compile_definitions(tst_HttpCacheRelay PRIVATE QTEST_FUNCTION_TIMEOUT=180000)
set_tests_properties(tst_HttpCacheRelay PROPERTIES TIMEOUT 240)
target_compile_definitions(tst_HttpCacheCorruptBan PRIVATE QTEST_FUNCTION_TIMEOUT=180000)
set_tests_properties(tst_HttpCacheCorruptBan PROPERTIES TIMEOUT 240)
emule_add_test(tst_HttpCacheConfigLink FOLDER "Protocol")
set_tests_properties(tst_HttpCacheConfigLink PROPERTIES TIMEOUT 120)
emule_add_test(tst_ED2KLink        FOLDER "Protocol")

# Module 29g: Server management
emule_add_test(tst_Server           FOLDER "Server")
emule_add_test(tst_ServerList       FOLDER "Server")
emule_add_test(tst_ServerConnect    FOLDER "Server")

# Module 29h: Client management & credits
emule_add_test(tst_ClientStateDefs    FOLDER "Client")
emule_add_test(tst_DeadSourceList     FOLDER "Client")
emule_add_test(tst_CorruptionBlackBox FOLDER "Client")
emule_add_test(tst_ClientCredits      FOLDER "Client")

# Module 9: UpDownClient, ClientList & URLClient
emule_add_test(tst_UpDownClient       FOLDER "Client")
emule_add_test(tst_ClientList         FOLDER "Client")
emule_add_test(tst_URLClient          FOLDER "Client")

# Module 29j: File management
emule_add_test(tst_AbstractFile       FOLDER "Files")
emule_add_test(tst_ShareableFile      FOLDER "Files")
emule_add_test(tst_StatisticFile      FOLDER "Files")
emule_add_test(tst_Collection         FOLDER "Files")
emule_add_test(tst_CollectionFile     FOLDER "Files")
emule_add_test(tst_KnownFile          FOLDER "Files")

# Module 29i: Kademlia
emule_add_test(tst_KadUInt128      FOLDER "Kademlia")
emule_add_test(tst_KadContact      FOLDER "Kademlia")
emule_add_test(tst_KadRoutingBin   FOLDER "Kademlia")
emule_add_test(tst_KadPrefs        FOLDER "Kademlia")
emule_add_test(tst_KadRoutingZone  FOLDER "Kademlia")

# Module 10: Kademlia / DHT — Complete Implementation
emule_add_test(tst_KadIO             FOLDER "Kademlia")
emule_add_test(tst_KadMiscUtils      FOLDER "Kademlia")
emule_add_test(tst_KadEntry          FOLDER "Kademlia")
emule_add_test(tst_KadLookupHistory  FOLDER "Kademlia")
emule_add_test(tst_KadPacketTracking FOLDER "Kademlia")
emule_add_test(tst_KadSearch         FOLDER "Kademlia")
emule_add_test(tst_KadSearchExpr     FOLDER "Kademlia")
emule_add_test(tst_KadSearchManager  FOLDER "Kademlia")
emule_add_test(tst_KadFirewallTester FOLDER "Kademlia")
emule_add_test(tst_KadIndexed        FOLDER "Kademlia")
emule_add_test(tst_KadUDPListener    FOLDER "Kademlia")
emule_add_test(tst_Kademlia          FOLDER "Kademlia")
emule_add_test(tst_SafeKadFastKad    FOLDER "Kademlia")

# Module 29o: Preferences & configuration
emule_add_test(tst_Preferences     FOLDER "Prefs")

# Module 29s: Media & Preview
emule_add_test(tst_MediaInfo       FOLDER "Media")

# Module 20: Media & Preview
emule_add_test(tst_PreviewApps     FOLDER "Media")
emule_add_test(tst_PreviewThread   FOLDER "Media")
emule_add_test(tst_FrameGrabThread FOLDER "Media")

# Module 11: PartFile & Download Integration
emule_add_test(tst_PartFile          FOLDER "Files")
emule_add_test(tst_SourceExchange       FOLDER "Files")
emule_add_test(tst_SourceExchangeCompat FOLDER "Files")
emule_add_test(tst_SourceSaver          FOLDER "Files")
emule_add_test(tst_DownloadQueue     FOLDER "Transfer")

# Module 11 Phase 3: Complete File Management
emule_add_test(tst_KnownFileList       FOLDER "Files")
emule_add_test(tst_SharedFileList      FOLDER "Files")
emule_add_test(tst_SharedFileLargeSet  FOLDER "Files")
target_compile_definitions(tst_SharedFileLargeSet PRIVATE QTEST_FUNCTION_TIMEOUT=300000)
set_tests_properties(tst_SharedFileLargeSet PROPERTIES TIMEOUT 300)
emule_add_test(tst_PartFileSharing  FOLDER "Files")
emule_add_test(tst_PartFileWriteThread FOLDER "Files")
emule_add_test(tst_PartFileConvert     FOLDER "Files")
emule_add_test(tst_ArchiveReader       FOLDER "Archive")
emule_add_test(tst_ArchiveRecovery     FOLDER "Archive")
emule_add_test(tst_ArchiveUnpack       FOLDER "Archive")

# Module 12: Upload Queue & Transfer Engine
emule_add_test(tst_UploadBandwidthThrottler FOLDER "Transfer")
emule_add_test(tst_UploadDiskIOThread       FOLDER "Transfer")
emule_add_test(tst_UploadQueue              FOLDER "Transfer")
emule_add_test(tst_UploadQueueStore         FOLDER "Transfer")
emule_add_test(tst_MockPeerUpload           FOLDER "Net")
target_compile_definitions(tst_MockPeerUpload PRIVATE QTEST_FUNCTION_TIMEOUT=150000)
set_tests_properties(tst_MockPeerUpload PROPERTIES TIMEOUT 150)
emule_add_test(tst_MockPeerDownload         FOLDER "Net")
target_compile_definitions(tst_MockPeerDownload PRIVATE QTEST_FUNCTION_TIMEOUT=300000)
set_tests_properties(tst_MockPeerDownload PROPERTIES TIMEOUT 300)
emule_add_test(tst_CallbackAndQueueRank     FOLDER "Net")
target_compile_definitions(tst_CallbackAndQueueRank PRIVATE QTEST_FUNCTION_TIMEOUT=60000)
set_tests_properties(tst_CallbackAndQueueRank PROPERTIES TIMEOUT 60)
emule_add_test(tst_CoreSessionHandlers      FOLDER "Net")
target_compile_definitions(tst_CoreSessionHandlers PRIVATE QTEST_FUNCTION_TIMEOUT=60000)
set_tests_properties(tst_CoreSessionHandlers PROPERTIES TIMEOUT 60)
emule_add_test(tst_Scheduler                FOLDER "Transfer")
emule_add_test(tst_ImportParts              FOLDER "Transfer")

# Module 13: Search Engine
emule_add_test(tst_SearchExpr    FOLDER "Search")
emule_add_test(tst_SearchParams  FOLDER "Search")
emule_add_test(tst_SearchFile    FOLDER "Search")
emule_add_test(tst_SearchList    FOLDER "Search")
emule_add_test(tst_SearchExprParser FOLDER "Search")
emule_add_test(tst_GlobalSearchScheduler FOLDER "Search")

# Module 14: IP Filter & Security
emule_add_test(tst_IPFilter      FOLDER "IPFilter")
emule_add_test(tst_IPFilterMatch FOLDER "IPFilter")

# Module 17: Chat & IRC
emule_add_test(tst_IrcProtocol   FOLDER "Chat")
emule_add_test(tst_IrcClient     FOLDER "Chat")

# Module 18: Friend System
emule_add_test(tst_Friend       FOLDER "Friends")
emule_add_test(tst_FriendList   FOLDER "Friends")

# Module 15: Statistics & Logging
emule_add_test(tst_Statistics      FOLDER "Stats")
emule_add_test(tst_StatisticsReset FOLDER "Stats")
emule_add_test(tst_StatsHistory    FOLDER "Stats")

# Module 19: Web Server
emule_add_test(tst_WebServer FOLDER "WebServer")

# IPC Protocol
emule_add_test(tst_IpcProtocol   FOLDER "Ipc" LIBRARIES eMule::Ipc)
emule_add_test(tst_IpcMessage    FOLDER "Ipc" LIBRARIES eMule::Ipc)
emule_add_test(tst_IpcConnection FOLDER "Ipc" LIBRARIES eMule::Ipc)
emule_add_test(tst_PreferencesIpc FOLDER "Ipc" LIBRARIES eMule::Ipc yaml-cpp::yaml-cpp)
# Push broadcast rate limiter — pure Qt Core, so it links the real library directly.
emule_add_test(tst_PushCoalescer FOLDER "Ipc" LIBRARIES eMule::Ipc)

# Data file integration tests — verify real server.met / nodes.dat / .part loading
emule_add_test(tst_ServerMetData  FOLDER "DataFiles")
emule_add_test(tst_KadNodesData   FOLDER "DataFiles")
emule_add_test(tst_PartFileData   FOLDER "DataFiles")
emule_add_test(tst_SharedFileData FOLDER "DataFiles")
emule_add_test(tst_WebServicesData FOLDER "DataFiles"
    SOURCES tst_WebServicesData.cpp "${CMAKE_SOURCE_DIR}/src/gui/utils/WebServices.cpp")
target_include_directories(tst_WebServicesData PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_WebServicesData PRIVATE Qt6::Widgets)

# eD2K link import skip policy — pulls in IpcClient, which Ed2kLinkImporter.cpp calls into,
# and HttpCacheLinkImporter, which it hands ed2k://|httpcache| links to.
emule_add_test(tst_Ed2kLinkImporter FOLDER "Ipc"
    SOURCES tst_Ed2kLinkImporter.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/utils/Ed2kLinkImporter.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/HttpCacheLinkImporter.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/StatusBarNotifier.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/app/IpcClient.cpp")
target_include_directories(tst_Ed2kLinkImporter PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_Ed2kLinkImporter PRIVATE eMule::Ipc Qt6::Widgets)

# Prev/Next walker behind the detail dialogs' arrow buttons. Depends on nothing but
# Qt Widgets, so the real ViewNavigation.cpp compiles straight against the stub pch.
emule_add_test(tst_ViewNavigation FOLDER "Gui"
    SOURCES tst_ViewNavigation.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/utils/ViewNavigation.cpp")
target_include_directories(tst_ViewNavigation PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_ViewNavigation PRIVATE Qt6::Widgets)

# Visibility-gated panel polling. Depends on nothing but Qt Widgets, same as above.
emule_add_test(tst_PanelPoller FOLDER "Gui"
    SOURCES tst_PanelPoller.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/utils/PanelPoller.cpp")
target_include_directories(tst_PanelPoller PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_PanelPoller PRIVATE Qt6::Widgets)

# Downloads Status column — the daemon sends the raw enum token ("ready", "empty") and
# the model turns it into MFC's wording. Pure string/int mapping, so the real source
# compiles standalone against the stub pch.
emule_add_test(tst_DownloadStatusText FOLDER "Gui"
    SOURCES tst_DownloadStatusText.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/controls/DownloadListModel.cpp")
target_include_directories(tst_DownloadStatusText PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_DownloadStatusText PRIVATE Qt6::Widgets)

# Tray icon download meter — pure painting, so the real source compiles standalone.
emule_add_test(tst_TrayMeterIcon FOLDER "Gui"
    SOURCES tst_TrayMeterIcon.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/controls/TrayMeterIcon.cpp")
target_include_directories(tst_TrayMeterIcon PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_TrayMeterIcon PRIVATE Qt6::Widgets)

# Link codec shared by the Server Info, chat and IRC panes — header-only, so nothing
# but the test itself is compiled. Widgets is for QTextDocument: one case replays the
# rich-text parse a QTextBrowser does.
emule_add_test(tst_TextLinks FOLDER "Ipc")
target_include_directories(tst_TextLinks PRIVATE "${CMAKE_SOURCE_DIR}/src/gui")
target_link_libraries(tst_TextLinks PRIVATE Qt6::Widgets)

# Version manifest parsing. VersionChecker's statics are pure, so no reply is needed.
# src/gui comes first on the include path, so VersionChecker.cpp picks up the real
# emuleqt pch.h — which pulls in QApplication, hence Widgets.
emule_add_test(tst_VersionChecker FOLDER "Ipc"
    SOURCES tst_VersionChecker.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/app/VersionChecker.cpp")
target_include_directories(tst_VersionChecker PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_VersionChecker PRIVATE Qt6::Network Qt6::Widgets)

# Live network integration tests (requires real network connectivity)
if(EMULE_LIVE_TESTS)
    emule_add_test(tst_KadLiveNetwork FOLDER "LiveNetwork")
    set_tests_properties(tst_KadLiveNetwork PROPERTIES LABELS "live" TIMEOUT 180)

    # Runs every case once per cache server it can reach, and skips the rest:
    # EMULE_HTTPCACHE_URL / EMULE_HTTPCACHE_KEY name one that is already running,
    # HTTPCACHE_GO_CMD a binary the test starts itself. Unset, it skips entirely.
    #
    # Both backends together take ~1.2s against localhost. The budget is sized for
    # an EMULE_HTTPCACHE_URL pointing somewhere real instead: 60s per case still
    # moves the 9.28 MB part over a link an order of magnitude slower than a LAN.
    emule_add_test(tst_HttpCacheLive FOLDER "LiveNetwork")
    target_compile_definitions(tst_HttpCacheLive PRIVATE QTEST_FUNCTION_TIMEOUT=60000)
    set_tests_properties(tst_HttpCacheLive PROPERTIES LABELS "live" TIMEOUT 120)

    emule_add_test(tst_FileDownloadLive FOLDER "LiveNetwork")
    target_compile_definitions(tst_FileDownloadLive PRIVATE QTEST_FUNCTION_TIMEOUT=960000)
    set_tests_properties(tst_FileDownloadLive PROPERTIES LABELS "live" TIMEOUT 960)

    emule_add_test(tst_ServerDownloadLive FOLDER "LiveNetwork")
    target_compile_definitions(tst_ServerDownloadLive PRIVATE QTEST_FUNCTION_TIMEOUT=120000)
    set_tests_properties(tst_ServerDownloadLive PROPERTIES LABELS "live" TIMEOUT 120)

    emule_add_test(tst_ServerGlobalSearchLive FOLDER "LiveNetwork")
    target_compile_definitions(tst_ServerGlobalSearchLive PRIVATE QTEST_FUNCTION_TIMEOUT=120000)
    set_tests_properties(tst_ServerGlobalSearchLive PROPERTIES LABELS "live" TIMEOUT 120)

    emule_add_test(tst_ServerConnectLive FOLDER "LiveNetwork")
    target_compile_definitions(tst_ServerConnectLive PRIVATE QTEST_FUNCTION_TIMEOUT=240000)
    set_tests_properties(tst_ServerConnectLive PROPERTIES LABELS "live" TIMEOUT 320)

    emule_add_test(tst_ServerLocalTest FOLDER "LiveNetwork")
    target_compile_definitions(tst_ServerLocalTest PRIVATE QTEST_FUNCTION_TIMEOUT=120000)
    set_tests_properties(tst_ServerLocalTest PROPERTIES LABELS "live" TIMEOUT 180)

    emule_add_test(tst_IrcLiveConnect FOLDER "LiveNetwork")
    target_compile_definitions(tst_IrcLiveConnect PRIVATE QTEST_FUNCTION_TIMEOUT=30000)
    set_tests_properties(tst_IrcLiveConnect PROPERTIES LABELS "live" TIMEOUT 30)

    emule_add_test(tst_SmtpSendEmail FOLDER "LiveNetwork")
    target_compile_definitions(tst_SmtpSendEmail PRIVATE QTEST_FUNCTION_TIMEOUT=60000)
    set_tests_properties(tst_SmtpSendEmail PROPERTIES LABELS "live" TIMEOUT 60)

    emule_add_test(tst_PortTestLive FOLDER "LiveNetwork")
    target_compile_definitions(tst_PortTestLive PRIVATE QTEST_FUNCTION_TIMEOUT=60000)
    set_tests_properties(tst_PortTestLive PROPERTIES LABELS "live" TIMEOUT 60)

    emule_add_test(tst_PortMapLive FOLDER "LiveNetwork")
    target_compile_definitions(tst_PortMapLive PRIVATE QTEST_FUNCTION_TIMEOUT=60000)
    set_tests_properties(tst_PortMapLive PROPERTIES LABELS "live" TIMEOUT 90)
endif()
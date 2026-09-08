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
emule_add_test(tst_ContainerSniffer   FOLDER "Media")
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
emule_add_test(tst_ConfigSeeding   FOLDER "Prefs" LIBRARIES yaml-cpp::yaml-cpp)
emule_add_test(tst_Preferences     FOLDER "Prefs")
emule_add_test(tst_CategoryPrefs   FOLDER "Prefs")

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

# Usenet (NNTP) module
emule_add_test(tst_UsenetSmoke FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_NntpSocket  FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_NntpServerPool FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_UsenetPrefs FOLDER "Usenet" LIBRARIES eMule::Usenet yaml-cpp::yaml-cpp)
emule_add_test(tst_UsenetYenc FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_UsenetNzbParse FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_UsenetArticleFetch FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_UsenetSharing FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_UsenetQueue FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_UsenetWatchFolder FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_UsenetNzbUrl FOLDER "Usenet" LIBRARIES eMule::Usenet)
# yaml-cpp direct, so the sidecar can be asserted to hold an *absolute* figure
# rather than an increment.
emule_add_test(tst_UsenetUsage FOLDER "Usenet" LIBRARIES eMule::Usenet yaml-cpp::yaml-cpp)

# Phase 6a. Separate from tst_UsenetQueue because it asserts on the *order* the
# scheduler asks in, not on the file that comes out; the two would otherwise
# share fixtures whose part sizes each wants to choose for itself.
emule_add_test(tst_UsenetStream FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_RarReader FOLDER "Usenet" LIBRARIES eMule::Usenet)

# The par2 cases build their own recovery sets with Par2::par2create, so this one
# needs the par2 headers that emuleusenet keeps PRIVATE. Linking the interface
# target again is harmless and is what makes a checked-in binary fixture
# unnecessary.
emule_add_test(tst_UsenetDirectUnpack FOLDER "Usenet" LIBRARIES eMule::Usenet)
emule_add_test(tst_UsenetUnpack FOLDER "Usenet" LIBRARIES eMule::Usenet)

# Phase 4 end to end: par2 fixtures are generated in-process, so this one needs
# the par2 headers too. The repair pass is real work, hence the raised timeout.
emule_add_test(tst_UsenetPostPipeline FOLDER "Usenet" LIBRARIES eMule::Usenet)
target_compile_definitions(tst_UsenetPostPipeline PRIVATE QTEST_FUNCTION_TIMEOUT=180000)
set_tests_properties(tst_UsenetPostPipeline PROPERTIES TIMEOUT 300)
emule_add_test(tst_UsenetPar2 FOLDER "Usenet" LIBRARIES eMule::Usenet)
if(EMULE_USENET_PAR2 AND TARGET emule_par2)
    target_link_libraries(tst_UsenetPar2 PRIVATE emule_par2)
endif()

# ---------------------------------------------------------------------------
# Indexer — the shared newznab / torznab search client
# ---------------------------------------------------------------------------

emule_add_test(tst_IndexerParse FOLDER "Indexer" LIBRARIES eMule::Indexer)
emule_add_test(tst_IndexerQuery FOLDER "Indexer" LIBRARIES eMule::Indexer)
emule_add_test(tst_IndexerSearch FOLDER "Indexer" LIBRARIES eMule::Indexer)
emule_add_test(tst_IndexerPrefs FOLDER "Indexer" LIBRARIES eMule::Indexer yaml-cpp::yaml-cpp)
emule_add_test(tst_IndexerFeedMatch FOLDER "Indexer" LIBRARIES eMule::Indexer)
# Links eMule::Indexer only, on purpose: the poller reaches the Usenet queue
# through an injected sink, and if it ever reached in directly this would
# stop linking.
emule_add_test(tst_IndexerFeedPoller FOLDER "Indexer" LIBRARIES eMule::Indexer)

# Whole recorded indexer answers from EMULE_NZB_DIR -- the same directory the
# live NZB download test takes releases from. Offline (it only reads files), so
# it is not a live test; it simply skips where that variable is unset.
emule_add_test(tst_IndexerFeed FOLDER "Indexer" LIBRARIES eMule::Indexer eMule::Usenet)

# Source-level GUI checks — no widgets, no linking of the GUI target
emule_add_test(tst_MenuIcons FOLDER "GUI")

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

# The chooser behind Preview on a multi-file Usenet archive set. Driven through
# applyListing() with hand-built CBOR, so it needs the real dialog but no daemon.
emule_add_test(tst_UsenetArchiveEntryDialog FOLDER "Gui"
    SOURCES tst_UsenetArchiveEntryDialog.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/dialogs/UsenetArchiveEntryDialog.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/FileTypeIcons.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/DialogSizing.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/controls/ContentScrollArea.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/app/UiState.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/app/IpcClient.cpp")
target_include_directories(tst_UsenetArchiveEntryDialog PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_UsenetArchiveEntryDialog PRIVATE
    eMule::Core eMule::Ipc Qt6::Widgets)

# The add/edit category dialog. Constructed directly with a DownloadCategory, so
# it needs the real dialog and DialogSizing but no daemon and no panel.
emule_add_test(tst_CategoryDialog FOLDER "Gui"
    SOURCES tst_CategoryDialog.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/dialogs/CategoryDialog.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/DialogSizing.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/controls/ContentScrollArea.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/app/UiState.cpp")
target_include_directories(tst_CategoryDialog PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_CategoryDialog PRIVATE
    eMule::Core eMule::Ipc Qt6::Widgets)

# The Options window's height budget. QStackedLayout's minimum is the MAX over its
# pages, so one page without a scroll area sets the window height for all eighteen --
# and the sidebar's grouping must not drift from the Page enum that uistate.yml stores.
# Compiles the real dialog; IpcClient is nullptr, so no daemon is involved.
emule_add_test(tst_OptionsDialogSizing FOLDER "Gui"
    SOURCES tst_OptionsDialogSizing.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/dialogs/OptionsDialog.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/dialogs/FirstStartWizard.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/panels/StatisticsPanel.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/controls/AccordionSidebar.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/controls/ContentScrollArea.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/controls/StatsGraph.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/DialogSizing.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/FileAssociation.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/PanelPoller.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/StatusBarNotifier.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/WebServices.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/app/AutoStart.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/app/Ed2kSchemeHandler.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/app/UiState.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/app/IpcClient.cpp"
            "${CMAKE_SOURCE_DIR}/resources/emuleqt.qrc")
target_include_directories(tst_OptionsDialogSizing PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_OptionsDialogSizing PRIVATE
    eMule::Core eMule::Ipc Qt6::Widgets)

# Visibility-gated panel polling. Depends on nothing but Qt Widgets, same as above.
emule_add_test(tst_PanelPoller FOLDER "Gui"
    SOURCES tst_PanelPoller.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/utils/PanelPoller.cpp")
target_include_directories(tst_PanelPoller PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_PanelPoller PRIVATE Qt6::Widgets)

# What counts as a droppable .nzb. Pure mime inspection, so the real source
# compiles standalone against the stub pch, same as above.
emule_add_test(tst_NzbDrop FOLDER "Gui"
    SOURCES tst_NzbDrop.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/utils/NzbDrop.cpp")
target_include_directories(tst_NzbDrop PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_NzbDrop PRIVATE Qt6::Widgets)

# The .nzb file-type registration. Only the two generators are exercised — the
# Windows and Linux writers compile on their own platform only, so this is the
# whole of what a developer machine can check.
emule_add_test(tst_FileAssociation FOLDER "Gui"
    SOURCES tst_FileAssociation.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/utils/FileAssociation.cpp")
target_include_directories(tst_FileAssociation PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_FileAssociation PRIVATE Qt6::Widgets)

# Downloads Status column — the daemon sends the raw enum token ("ready", "empty") and
# the model turns it into MFC's wording. Pure string/int mapping, so the real source
# compiles standalone against the stub pch.
emule_add_test(tst_DownloadStatusText FOLDER "Gui"
    SOURCES tst_DownloadStatusText.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/controls/DownloadListModel.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/RatingIcons.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/FileTypeIcons.cpp")
target_include_directories(tst_DownloadStatusText PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_DownloadStatusText PRIVATE Qt6::Widgets)

# Column-0 marks: the rating icon and the red "not what it claims" exclamation.
# Same standalone-source trick as tst_DownloadStatusText above.
emule_add_test(tst_FileMarks FOLDER "Gui"
    SOURCES tst_FileMarks.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/controls/DownloadListModel.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/controls/SharedFilesModel.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/RatingIcons.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/FileTypeIcons.cpp"
            "${CMAKE_SOURCE_DIR}/resources/emuleqt.qrc")
target_include_directories(tst_FileMarks PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_FileMarks PRIVATE Qt6::Widgets)

# The Comments page and its editable subclass. Both are plain widgets over a details
# CBOR map — no daemon, no panel — so the real sources compile standalone against the
# stub pch, same trick as tst_FileMarks above.
emule_add_test(tst_CommentsPanel FOLDER "Gui"
    SOURCES tst_CommentsPanel.cpp
            "${CMAKE_SOURCE_DIR}/src/gui/dialogs/CommentsPanel.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/dialogs/CommentEditPanel.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/app/UiState.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/RatingIcons.cpp"
            "${CMAKE_SOURCE_DIR}/src/gui/utils/FileTypeIcons.cpp"
            "${CMAKE_SOURCE_DIR}/resources/emuleqt.qrc")
target_include_directories(tst_CommentsPanel PRIVATE
    "${CMAKE_SOURCE_DIR}/src/gui"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(tst_CommentsPanel PRIVATE Qt6::Widgets)

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
#
# Per-function budgets are set through the test's ENVIRONMENT, not as a compile
# definition: QtTest reads QTEST_FUNCTION_TIMEOUT from the environment at run
# time, so `target_compile_definitions(... QTEST_FUNCTION_TIMEOUT=N)` compiles
# cleanly and changes nothing — a long live case then dies at Qt's 300 s default
# with "Received a fatal error". APPEND, so emule_add_test's QT_QPA_PLATFORM
# survives.
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
    set_tests_properties(tst_HttpCacheLive PROPERTIES LABELS "live" TIMEOUT 120)
    set_property(TEST tst_HttpCacheLive APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=60000")

    emule_add_test(tst_FileDownloadLive FOLDER "LiveNetwork")
    set_tests_properties(tst_FileDownloadLive PROPERTIES LABELS "live" TIMEOUT 960)
    set_property(TEST tst_FileDownloadLive APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=960000")

    emule_add_test(tst_ServerDownloadLive FOLDER "LiveNetwork")
    set_tests_properties(tst_ServerDownloadLive PROPERTIES LABELS "live" TIMEOUT 120)
    set_property(TEST tst_ServerDownloadLive APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=120000")

    emule_add_test(tst_ServerGlobalSearchLive FOLDER "LiveNetwork")
    set_tests_properties(tst_ServerGlobalSearchLive PROPERTIES LABELS "live" TIMEOUT 120)
    set_property(TEST tst_ServerGlobalSearchLive APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=120000")

    emule_add_test(tst_ServerConnectLive FOLDER "LiveNetwork")
    set_tests_properties(tst_ServerConnectLive PROPERTIES LABELS "live" TIMEOUT 320)
    set_property(TEST tst_ServerConnectLive APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=240000")

    emule_add_test(tst_ServerLocalTest FOLDER "LiveNetwork")
    set_tests_properties(tst_ServerLocalTest PROPERTIES LABELS "live" TIMEOUT 180)
    set_property(TEST tst_ServerLocalTest APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=120000")

    emule_add_test(tst_IrcLiveConnect FOLDER "LiveNetwork")
    set_tests_properties(tst_IrcLiveConnect PROPERTIES LABELS "live" TIMEOUT 30)
    set_property(TEST tst_IrcLiveConnect APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=30000")

    # Needs EMULE_INDEXER_APIKEY / _URL / _QUERY, from .env or the environment,
    # and skips without them. Nothing about the account is checked in — no key,
    # and no indexer URL either; see the header comment in the test.
    # Gated like every other live test: a search spends the user's paid API
    # quota, so a default `ctest` must not be able to reach it.
    emule_add_test(tst_IndexerLiveSearch FOLDER "Indexer" LIBRARIES eMule::Indexer)
    set_tests_properties(tst_IndexerLiveSearch PROPERTIES LABELS "live" TIMEOUT 120)

    emule_add_test(tst_UsenetLiveConnect FOLDER "LiveNetwork" LIBRARIES eMule::Usenet)
    set_tests_properties(tst_UsenetLiveConnect PROPERTIES LABELS "live" TIMEOUT 120)
    set_property(TEST tst_UsenetLiveConnect APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=60000")

    emule_add_test(tst_UsenetLiveFetch FOLDER "LiveNetwork" LIBRARIES eMule::Usenet)
    set_tests_properties(tst_UsenetLiveFetch PROPERTIES LABELS "live" TIMEOUT 1200)
    set_property(TEST tst_UsenetLiveFetch APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=900000")

    # Every .nzb in EMULE_NZB_DIR, downloaded through the real queue and
    # unpacked. Skips without EMULE_NNTP_HOST / EMULE_NZB_DIR; see the header
    # comment. Sized for a multi-GB release on a slow line -- the test's own
    # per-release budget (EMULE_NZB_TIMEOUT_MIN) is what normally stops it.
    emule_add_test(tst_UsenetLiveDownload FOLDER "LiveNetwork" LIBRARIES eMule::Usenet)
    set_tests_properties(tst_UsenetLiveDownload PROPERTIES LABELS "live" TIMEOUT 7200)
    set_property(TEST tst_UsenetLiveDownload APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=3600000")

    # The same releases, previewed while they download. Cheap by comparison: it
    # fetches the head of one file, not the release.
    emule_add_test(tst_UsenetLivePreview FOLDER "LiveNetwork" LIBRARIES eMule::Usenet)
    set_tests_properties(tst_UsenetLivePreview PROPERTIES LABELS "live" TIMEOUT 1800)
    set_property(TEST tst_UsenetLivePreview APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=900000")

    emule_add_test(tst_SmtpSendEmail FOLDER "LiveNetwork")
    set_tests_properties(tst_SmtpSendEmail PROPERTIES LABELS "live" TIMEOUT 60)
    set_property(TEST tst_SmtpSendEmail APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=60000")

    emule_add_test(tst_PortTestLive FOLDER "LiveNetwork")
    set_tests_properties(tst_PortTestLive PROPERTIES LABELS "live" TIMEOUT 60)
    set_property(TEST tst_PortTestLive APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=60000")

    emule_add_test(tst_PortMapLive FOLDER "LiveNetwork")
    set_tests_properties(tst_PortMapLive PROPERTIES LABELS "live" TIMEOUT 90)
    set_property(TEST tst_PortMapLive APPEND PROPERTY ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=60000")
endif()
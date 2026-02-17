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
target_compile_definitions(emule_test_helpers INTERFACE
    EMULE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
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
        set(ARG_SOURCES "${TEST_NAME}.cpp")
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

# Module 29c: Cryptography
emule_add_test(tst_MD4              FOLDER "Crypto")
emule_add_test(tst_MD5              FOLDER "Crypto")
emule_add_test(tst_SHA              FOLDER "Crypto")
emule_add_test(tst_AICHHashTree     FOLDER "Crypto")
emule_add_test(tst_AICHHashSet      FOLDER "Crypto")
emule_add_test(tst_FileIdentifier   FOLDER "Crypto")

# Module 29d: Networking
emule_add_test(tst_Packet            FOLDER "Net")
emule_add_test(tst_EncryptedDatagram FOLDER "Net")
emule_add_test(tst_EMSocket          FOLDER "Net")
emule_add_test(tst_Pinger            FOLDER "Net")

# Module 5: Remaining networking classes
emule_add_test(tst_LastCommonRouteFinder FOLDER "Net")
emule_add_test(tst_ServerSocket          FOLDER "Net")
emule_add_test(tst_UDPSocket             FOLDER "Net")
emule_add_test(tst_ListenSocket          FOLDER "Net")
emule_add_test(tst_HttpClientReqSocket   FOLDER "Net")
emule_add_test(tst_ClientUDPSocket       FOLDER "Net")

# Module 29e: Firewall & UPnP
emule_add_test(tst_UPnPManager   FOLDER "UPnP")

# Module 29f: Protocol
emule_add_test(tst_Tags            FOLDER "Protocol")
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
emule_add_test(tst_KadSearchManager  FOLDER "Kademlia")
emule_add_test(tst_KadFirewallTester FOLDER "Kademlia")
emule_add_test(tst_KadIndexed        FOLDER "Kademlia")
emule_add_test(tst_KadUDPListener    FOLDER "Kademlia")
emule_add_test(tst_Kademlia          FOLDER "Kademlia")

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
emule_add_test(tst_DownloadQueue     FOLDER "Transfer")

# Module 11 Phase 3: Complete File Management
emule_add_test(tst_KnownFileList       FOLDER "Files")
emule_add_test(tst_SharedFileList      FOLDER "Files")
emule_add_test(tst_PartFileWriteThread FOLDER "Files")
emule_add_test(tst_PartFileConvert     FOLDER "Files")
emule_add_test(tst_ArchiveReader       FOLDER "Archive")
emule_add_test(tst_ArchiveRecovery     FOLDER "Archive")

# Module 12: Upload Queue & Transfer Engine
emule_add_test(tst_UploadBandwidthThrottler FOLDER "Transfer")
emule_add_test(tst_UploadDiskIOThread       FOLDER "Transfer")
emule_add_test(tst_UploadQueue              FOLDER "Transfer")
emule_add_test(tst_Scheduler                FOLDER "Transfer")
emule_add_test(tst_ImportParts              FOLDER "Transfer")

# Module 13: Search Engine
emule_add_test(tst_SearchExpr    FOLDER "Search")
emule_add_test(tst_SearchParams  FOLDER "Search")
emule_add_test(tst_SearchFile    FOLDER "Search")
emule_add_test(tst_SearchList    FOLDER "Search")
emule_add_test(tst_SearchExprParser FOLDER "Search")

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

# Module 19: Web Server
emule_add_test(tst_WebServer FOLDER "WebServer")
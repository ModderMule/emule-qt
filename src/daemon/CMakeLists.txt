# ---------------------------------------------------------------------------
# emulecored — headless core daemon executable
#
# Links against libemulecore + libemuleipc + libemuleindexer + libemuleusenet.
# Runs as a QCoreApplication (no GUI).
#
# Everything but main.cpp lives in the static lib emuledaemon, so tests link
# the objects instead of compiling their own copies.
# ---------------------------------------------------------------------------

file(GLOB DAEMON_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")
list(REMOVE_ITEM DAEMON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

add_library(emuledaemon STATIC ${DAEMON_SOURCES})
add_library(eMule::Daemon ALIAS emuledaemon)

target_include_directories(emuledaemon
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(emuledaemon
    PUBLIC
        eMule::Core
        eMule::Ipc
        eMule::Indexer
        eMule::Usenet
        eMule::CommonSettings
)

qt_add_executable(emulecored main.cpp)

# Whole archive: the exe keeps every object, as when it compiled them itself
target_link_libraries(emulecored
    PRIVATE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,emuledaemon>"
        eMule::CommonSettings
)

# BitTorrent: optional; BtBridge answers 503 without it
if(TARGET eMule::Bt)
    target_link_libraries(emuledaemon PUBLIC eMule::Bt)
endif()

# ed3k (packmule): optional
if(TARGET eMule::Ed3k)
    target_link_libraries(emuledaemon PUBLIC eMule::Ed3k)
endif()

# WinShutdownHandler: the hidden window that hears about logoff and shutdown
if(WIN32)
    target_link_libraries(emuledaemon PUBLIC user32)
endif()

# PowerManager: idle-sleep prevention
if(APPLE)
    target_link_libraries(emuledaemon PUBLIC "-framework IOKit" "-framework CoreFoundation")
elseif(LINUX)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(emuledaemon PUBLIC Qt6::DBus)
elseif(WIN32)
    # Same donkey as the GUI: Explorer, and the console window when it has one.
    set(APP_ICON_RC "${CMAKE_SOURCE_DIR}/resources/appicon.rc")
    target_sources(emulecored PRIVATE "${APP_ICON_RC}")
    set_source_files_properties("${APP_ICON_RC}" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/resources/icons/MuleApp.ico")
endif()

set_target_properties(emulecored emuledaemon PROPERTIES
    FOLDER "Daemon"
)

# specialdlp is loaded at runtime, not linked: keep a copy next to the daemon
if(TARGET specialdlp)
    add_dependencies(emulecored specialdlp)
    add_custom_command(TARGET emulecored POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:specialdlp> $<TARGET_FILE_DIR:emulecored>
        VERBATIM)
endif()

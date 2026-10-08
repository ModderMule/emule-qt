# ---------------------------------------------------------------------------
# emulecored — headless core daemon executable
#
# Links against libemulecore + libemuleipc + libemuleindexer + libemuleusenet.
# Runs as a QCoreApplication (no GUI).
# ---------------------------------------------------------------------------

file(GLOB DAEMON_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

qt_add_executable(emulecored ${DAEMON_SOURCES})

target_include_directories(emulecored
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(emulecored
    PRIVATE
        eMule::Core
        eMule::Ipc
        eMule::Indexer
        eMule::Usenet
        eMule::CommonSettings
)

# WinShutdownHandler: the hidden window that hears about logoff and shutdown
if(WIN32)
    target_link_libraries(emulecored PRIVATE user32)
endif()

# PowerManager: idle-sleep prevention
if(APPLE)
    target_link_libraries(emulecored PRIVATE "-framework IOKit" "-framework CoreFoundation")
elseif(LINUX)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(emulecored PRIVATE Qt6::DBus)
elseif(WIN32)
    # Same donkey as the GUI: Explorer, and the console window when it has one.
    set(APP_ICON_RC "${CMAKE_SOURCE_DIR}/resources/appicon.rc")
    target_sources(emulecored PRIVATE "${APP_ICON_RC}")
    set_source_files_properties("${APP_ICON_RC}" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/resources/icons/MuleApp.ico")
endif()

set_target_properties(emulecored PROPERTIES
    FOLDER "Daemon"
)

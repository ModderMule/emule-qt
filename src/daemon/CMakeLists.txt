# ---------------------------------------------------------------------------
# emulecored — headless core daemon executable
#
# Links against libemulecore + libemuleipc.
# Runs as a QCoreApplication (no GUI).
# ---------------------------------------------------------------------------

file(GLOB_RECURSE DAEMON_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

qt_add_executable(emulecored ${DAEMON_SOURCES})

target_include_directories(emulecored
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(emulecored
    PRIVATE
        eMule::Core
        eMule::Ipc
        eMule::CommonSettings
)

set_target_properties(emulecored PROPERTIES
    FOLDER "Daemon"
)

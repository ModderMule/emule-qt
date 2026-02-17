# ---------------------------------------------------------------------------
# emuleqt — GUI application executable
#
# Links against libemulecore + Qt6::Widgets.
# ---------------------------------------------------------------------------

# -- Sub-module source lists ------------------------------------------------

# Module 21: Main application shell
file(GLOB_RECURSE APP_SOURCES      CONFIGURE_DEPENDS "app/*.cpp"      "app/*.h")

# Module 22: Tab/page panels
file(GLOB_RECURSE PANEL_SOURCES    CONFIGURE_DEPENDS "panels/*.cpp"   "panels/*.h")

# Module 23: Custom controls
file(GLOB_RECURSE CONTROL_SOURCES  CONFIGURE_DEPENDS "controls/*.cpp" "controls/*.h")

# Module 24: Dialogs
file(GLOB_RECURSE DIALOG_SOURCES   CONFIGURE_DEPENDS "dialogs/*.cpp"  "dialogs/*.h")

# Module 25: Graphics utilities
file(GLOB_RECURSE GFX_SOURCES      CONFIGURE_DEPENDS "graphics/*.cpp" "graphics/*.h")

# -- Qt resources -----------------------------------------------------------

set(RESOURCES
    "${CMAKE_SOURCE_DIR}/resources/emuleqt.qrc"
)

# -- Executable target ------------------------------------------------------

qt_add_executable(emuleqt
    ${APP_SOURCES}
    ${PANEL_SOURCES}
    ${CONTROL_SOURCES}
    ${DIALOG_SOURCES}
    ${GFX_SOURCES}
    ${RESOURCES}
)

target_include_directories(emuleqt
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(emuleqt
    PRIVATE
        eMule::Core
        Qt6::Widgets
        eMule::CommonSettings
)

# macOS bundle settings
set_target_properties(emuleqt PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "eMule Qt"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.emule.emuleqt"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    WIN32_EXECUTABLE TRUE
)

# ToDo: Add target_precompile_headers(emuleqt REUSE_FROM emulecore) when GUI has more source files

# Source groups for IDE navigation
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${APP_SOURCES} ${PANEL_SOURCES} ${CONTROL_SOURCES}
    ${DIALOG_SOURCES} ${GFX_SOURCES}
)
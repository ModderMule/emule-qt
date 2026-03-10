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
        eMule::Ipc
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::Network
        yaml-cpp::yaml-cpp
        eMule::CommonSettings
)

if(APPLE)
    target_link_libraries(emuleqt PRIVATE "-framework IOKit" "-framework CoreFoundation" "-framework CoreServices")
elseif(LINUX)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(emuleqt PRIVATE Qt6::DBus)
endif()

# macOS bundle icon
if(APPLE)
    set(MULE_ICNS "${CMAKE_SOURCE_DIR}/resources/icons/Mule.icns")
    target_sources(emuleqt PRIVATE "${MULE_ICNS}")
    set_source_files_properties("${MULE_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
endif()

# macOS bundle settings
set_target_properties(emuleqt PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "eMule Qt"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.emule.emuleqt"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_ICON_FILE "Mule.icns"
    WIN32_EXECUTABLE TRUE
)

# -- Precompiled header -------------------------------------------------------
# Independent PCH for the GUI — cannot REUSE_FROM emulecore because emuleqt
# links Qt6::Widgets (different include paths / compile flags).
target_precompile_headers(emuleqt PRIVATE
    # C++ standard library
    <algorithm>
    <vector>
    # Qt Core
    <QByteArray>
    <QCborArray>
    <QCborMap>
    <QDateTime>
    <QList>
    <QObject>
    <QString>
    <QStringList>
    <QTimer>
    # Qt Widgets (GUI-specific, high frequency)
    <QAbstractTableModel>
    <QApplication>
    <QDialog>
    <QFont>
    <QHBoxLayout>
    <QHeaderView>
    <QIcon>
    <QLabel>
    <QLineEdit>
    <QMenu>
    <QPainter>
    <QPushButton>
    <QSortFilterProxyModel>
    <QSplitter>
    <QTreeView>
    <QVBoxLayout>
    <QWidget>
)

# -- Translations (i18n) ----------------------------------------------------
# Compile any .ts files found in lang/ into .qm and copy to build dir.
# To extract strings or add languages, use: ./scripts/localize.sh

file(GLOB TS_FILES "${CMAKE_SOURCE_DIR}/lang/emuleqt_*.ts")
if(TS_FILES)
    find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
    qt_add_translations(emuleqt
        TS_FILES ${TS_FILES}
        QM_FILES_OUTPUT_VARIABLE QM_FILES
    )
endif()

# Source groups for IDE navigation
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${APP_SOURCES} ${PANEL_SOURCES} ${CONTROL_SOURCES}
    ${DIALOG_SOURCES} ${GFX_SOURCES}
)
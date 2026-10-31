# ---------------------------------------------------------------------------
# emuleqt-updater — installs a downloaded update package
#
# The daemon copies it out of the install dir and starts it detached; it waits
# for core + GUIs to quit, replaces the install, restarts them. Plain C++ and OS
# APIs only: no Qt, since it replaces the Qt libs, and on Linux it runs without
# the bundle's LD_LIBRARY_PATH launcher.
# ---------------------------------------------------------------------------

add_executable(emuleqt-updater
    main.cpp
    Updater.cpp
    Updater.h
    Platform.h
    $<IF:$<PLATFORM_ID:Windows>,PlatformWin.cpp,PlatformPosix.cpp>
)

target_link_libraries(emuleqt-updater PRIVATE eMule::Warnings)

set_target_properties(emuleqt-updater PROPERTIES
    FOLDER "Daemon"
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

if(MSVC)
    # static CRT: the copy in <configDir>/update has no VC runtime DLLs next to it
    set_property(TARGET emuleqt-updater PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(WIN32)
    target_compile_definitions(emuleqt-updater PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(emuleqt-updater PRIVATE shell32 ole32)
    if(MINGW)
        target_link_options(emuleqt-updater PRIVATE -municode)
    endif()
elseif(NOT APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # no runtime deps on the bundle's (or the system's) libstdc++
    target_link_options(emuleqt-updater PRIVATE -static-libstdc++ -static-libgcc)
endif()

# ---------------------------------------------------------------------------
# WinResources.cmake — per-exe .rc (icon + VERSIONINFO) on Windows
# ---------------------------------------------------------------------------

# emule_add_win_resources(<target> <rc>): adds <rc> to <target>. The per-exe rc
# files include appicon.rc/versioninfo.rc and config_win.h (version), which
# rc's own dependency scan misses with some generators -> listed explicitly.
function(emule_add_win_resources target rc)
    if(NOT WIN32)
        return()
    endif()
    target_sources(${target} PRIVATE "${rc}")
    set_source_files_properties("${rc}" PROPERTIES OBJECT_DEPENDS
        "${CMAKE_SOURCE_DIR}/resources/appicon.rc;${CMAKE_SOURCE_DIR}/resources/versioninfo.rc;${CMAKE_SOURCE_DIR}/resources/icons/MuleApp.ico;${CMAKE_SOURCE_DIR}/src/core/config_win.h")
endfunction()

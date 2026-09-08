# ---------------------------------------------------------------------------
# CompilerSettings.cmake — project-wide C++23 and compiler hardening
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6 requires MOC/UIC/RCC — enable globally
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Position-independent code (required for shared libs and Qt plugins)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------
add_library(emule_warnings INTERFACE)
add_library(eMule::Warnings ALIAS emule_warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(emule_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wcast-align
        -Wunused
        -Wnull-dereference
        -Wformat=2
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(emule_warnings INTERFACE
            -Wno-c++98-compat
            -Wno-c++98-compat-pedantic
        )
    endif()
elseif(MSVC)
    target_compile_options(emule_warnings INTERFACE
        /W4
        /permissive-
        /utf-8
    )
endif()

# ---------------------------------------------------------------------------
# Debug info for Release builds — enables useful crash dump stack traces
# ---------------------------------------------------------------------------
option(EMULE_RELEASE_DEBUG_INFO "Include debug symbols in Release builds (for crash dumps)" ON)

if(EMULE_RELEASE_DEBUG_INFO)
    if(MSVC)
        # /Z7 (debug info embedded in each .obj) rather than /Zi (a separate
        # compiler-side PDB): ccache cannot cache /Zi output, which made every CI
        # compile uncacheable — "Cacheable calls: 0 / 522 (0.00%)".  The final
        # program database is produced by the linker from /DEBUG below either way,
        # so crash-dump quality is unchanged; /Z7 only costs larger .obj files, and
        # it drops the mspdbsrv.exe serialisation that /Zi needs for parallel
        # compiles.
        add_compile_options("$<$<CONFIG:Release>:/Z7>")
        add_link_options("$<$<CONFIG:Release>:/DEBUG>" "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options("$<$<CONFIG:Release>:-g>")
    endif()
endif()

# ---------------------------------------------------------------------------
# Platform definitions
# ---------------------------------------------------------------------------
add_library(emule_platform INTERFACE)
add_library(eMule::Platform ALIAS emule_platform)

target_compile_definitions(emule_platform INTERFACE
    # Qt recommended defines
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_USE_QSTRINGBUILDER
    QT_STRICT_ITERATORS
    # Project
    SUPPORT_LARGE_FILES
    $<$<CONFIG:Debug>:EMULE_DEBUG>
    $<$<CONFIG:Debug>:EMULE_DEV_BUILD>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:UNICODE>
    $<$<PLATFORM_ID:Windows>:_UNICODE>
)

# Where a locally-built binary finds data/config. Packaged builds ship their own
# copy (.app Resources/config, or config/ next to the exe) and should configure
# with -DEMULE_SEED_FROM_SOURCE_TREE=OFF so no build-machine path is baked in.
# Defined either way, empty when off, so the seeding code compiles identically
# everywhere -- CI builds with tests off and would never catch an #ifdef'd block.
option(EMULE_SEED_FROM_SOURCE_TREE
       "Let a locally-built binary seed config data from the repo's data/config" ON)
if(EMULE_SEED_FROM_SOURCE_TREE)
    set(_emule_source_config "${PROJECT_SOURCE_DIR}/data/config")
else()
    set(_emule_source_config "")
endif()
target_compile_definitions(emule_platform INTERFACE
    EMULE_SOURCE_CONFIG_DIR="${_emule_source_config}")

# ---------------------------------------------------------------------------
# Convenience "all common settings" target
# ---------------------------------------------------------------------------
add_library(emule_common_settings INTERFACE)
add_library(eMule::CommonSettings ALIAS emule_common_settings)

target_link_libraries(emule_common_settings INTERFACE
    eMule::Warnings
    eMule::Platform
    emule_generated_config
)
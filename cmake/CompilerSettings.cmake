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
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:UNICODE>
    $<$<PLATFORM_ID:Windows>:_UNICODE>
)

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
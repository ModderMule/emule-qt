# ---------------------------------------------------------------------------
# libemuleipc — shared IPC protocol library (no emulecore dependency)
#
# Links only against Qt6::Core + Qt6::Network.
# Both daemon and GUI link this library.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE IPC_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

# Exclude CborSerializers.h from this library — it depends on core types
# and is included directly by daemon code that also links eMule::Core.
list(FILTER IPC_SOURCES EXCLUDE REGEX "CborSerializers\\.h$")

add_library(emuleipc STATIC ${IPC_SOURCES})
add_library(eMule::Ipc ALIAS emuleipc)

target_include_directories(emuleipc
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(emuleipc
    PUBLIC
        Qt6::Core
        Qt6::Network
        OpenSSL::Crypto
)

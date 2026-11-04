# ---------------------------------------------------------------------------
# emuleqt-i2pd — the bundled I2P router for BitTorrent over I2P (btI2pMode 1)
#
# A prebuilt release of github.com/ed3k-protocol/i2pd (its emuleqt-release.yml), pinned
# with SHA-256 in scripts/i2pd.ref. Downloaded at configure time into
# ${CMAKE_BINARY_DIR}/i2pd: emuleqt-i2pd[.exe], certificates/, LICENSE. The daemon finds
# it there (I2pRouter::findBinary walks up from its binary); the bundlers copy it.
# Not pinned, no asset for this platform, or offline: a warning, and only an external
# router can be used.
# ---------------------------------------------------------------------------
set(_i2pd_dir "${CMAKE_BINARY_DIR}/i2pd")

if(APPLE)
    set(_i2pd_asset "i2pd-macos-universal.tar.gz")
elseif(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    set(_i2pd_asset "i2pd-windows-x64.zip")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(_i2pd_asset "i2pd-linux-x86_64.tar.gz")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_i2pd_asset "i2pd-linux-aarch64.tar.gz")
else()
    message(WARNING "emuleqt-i2pd: no release for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}; external I2P router only")
    return()
endif()

file(STRINGS "${CMAKE_SOURCE_DIR}/scripts/i2pd.ref" _i2pd_ref REGEX "^[^#]")
set(_i2pd_tag "")
set(_i2pd_sha "")
foreach(_line IN LISTS _i2pd_ref)
    if(_line MATCHES "^tag[ \t]+([^ \t]+)")
        set(_i2pd_tag "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "^${_i2pd_asset}[ \t]+([0-9a-fA-F]+)$")
        set(_i2pd_sha "${CMAKE_MATCH_1}")
    endif()
endforeach()

if(NOT _i2pd_tag OR NOT _i2pd_sha MATCHES "^[0-9a-fA-F]+$" OR NOT _i2pd_sha MATCHES "^.{64}$")
    message(WARNING "emuleqt-i2pd: ${_i2pd_asset} not pinned in scripts/i2pd.ref; external I2P router only")
    return()
endif()

# once per pin
set(_i2pd_stamp "${_i2pd_dir}/.pinned-${_i2pd_tag}-${_i2pd_sha}")
if(NOT EXISTS "${_i2pd_stamp}")
    set(_i2pd_url "https://github.com/ed3k-protocol/i2pd/releases/download/${_i2pd_tag}/${_i2pd_asset}")
    set(_i2pd_archive "${CMAKE_BINARY_DIR}/_deps/${_i2pd_asset}")
    message(STATUS "emuleqt-i2pd: ${_i2pd_tag} (${_i2pd_asset})")
    file(DOWNLOAD "${_i2pd_url}" "${_i2pd_archive}" EXPECTED_HASH SHA256=${_i2pd_sha} STATUS _i2pd_status TLS_VERIFY ON)
    list(GET _i2pd_status 0 _i2pd_code)
    if(NOT _i2pd_code EQUAL 0)
        list(GET _i2pd_status 1 _i2pd_msg)
        message(WARNING "emuleqt-i2pd: download failed (${_i2pd_msg}); external I2P router only")
        file(REMOVE "${_i2pd_archive}")
        return()
    endif()
    file(REMOVE_RECURSE "${_i2pd_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_i2pd_archive}" DESTINATION "${_i2pd_dir}")
    file(TOUCH "${_i2pd_stamp}")
endif()
message(STATUS "emuleqt-i2pd: ${_i2pd_dir}")

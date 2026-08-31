# rapidyenc — decoder-only static build.
#
# Upstream has no install() rules at all, so everything below the build step is
# done by hand. It also builds only static/shared object libraries named
# rapidyenc_static / rapidyenc_shared; we take the static one and install it
# under the plain name the CMake side searches for with find_library().

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO animetosho/rapidyenc
    REF v1.1.1
    SHA512 22786ee23e7f5aaabf0b08989339713dab4ada64893f64243387ec556d753264e285643e2eb0810450716441446b99e15bf382e6552c4ca5dfb457857371851e
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        # We decode; we never post. Dropping the encoder is most of the build.
        -DDISABLE_ENCODE=ON
        -DDISABLE_DECODE=OFF
        -DDISABLE_CRC=OFF
        # Never BUILD_NATIVE: a binary tuned to the build machine's CPU is not
        # redistributable, and this one ships in a release archive.
        -DBUILD_NATIVE=OFF
)

vcpkg_cmake_build()

file(INSTALL "${SOURCE_PATH}/rapidyenc.h"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include")

file(GLOB _rapidyenc_libs
     "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/*rapidyenc_static*"
     "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/Release/*rapidyenc_static*")
foreach(_lib IN LISTS _rapidyenc_libs)
    get_filename_component(_ext "${_lib}" LAST_EXT)
    if(_ext STREQUAL ".lib" OR _ext STREQUAL ".a")
        file(INSTALL "${_lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
             RENAME "rapidyenc${_ext}")
    endif()
endforeach()

file(GLOB _rapidyenc_dbg
     "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/*rapidyenc_static*"
     "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/Debug/*rapidyenc_static*")
foreach(_lib IN LISTS _rapidyenc_dbg)
    get_filename_component(_ext "${_lib}" LAST_EXT)
    if(_ext STREQUAL ".lib" OR _ext STREQUAL ".a")
        file(INSTALL "${_lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib"
             RENAME "rapidyenc${_ext}")
    endif()
endforeach()

# rapidyenc itself is CC0 and ships no licence file; README.md carries the
# statement. crcutil, which it vendors, has its own Apache-2.0 file.
vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/README.md"
    "${SOURCE_PATH}/crcutil-1.0/LICENSE")

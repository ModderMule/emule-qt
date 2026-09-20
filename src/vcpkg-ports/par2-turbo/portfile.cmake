# par2-turbo — the nzbgetcom fork of par2cmdline-turbo, built as a library.
#
# The fork rather than animetosho's original: only this one carries BUILD_LIB /
# BUILD_TOOL and an include/par2 tree. Upstream builds a CLI and cannot be
# consumed as a library at all. NZBGet ships exactly this dependency.
#
# Four things this port has to do by hand, all of which are load-bearing:
#
#  1. There are no install() rules, so the archives and headers are copied out of
#     the build tree explicitly.
#  2. config.h is *generated* into the build directory and is installed beside
#     the headers. libpar2.h picks its u32/u64 typedefs from it under
#     HAVE_CONFIG_H, and on LP64 that is uint64_t rather than unsigned long long
#     — same width, different mangled name. A consumer that compiles without the
#     matching config.h gets an undefined reference to Process().
#  3. The library is built -fno-rtti (its cmake/common.cmake, non-MSVC branch
#     only -- under MSVC it keeps the triplet's /GR), so its classes
#     have vtables and no typeinfo. Anything subclassing Par2Repairer must be
#     compiled the same way; src/usenet/CMakeLists.txt does that for
#     Par2Verifier.cpp.
#  4. That same common.cmake hardcodes /MT for MSVC Release, because upstream
#     ships a standalone CLI. In a library that is LNK2038 against every object
#     built with the triplet's dynamic CRT, so fix-msvc-crt.patch drops the flag
#     and lets the triplet decide.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO nzbgetcom/par2cmdline-turbo
    REF v1.4.0-20260803
    SHA512 0feae4b62477c8e3c43f35cb9d025f61ecfac1304c53bb76355c79ef4861cfe631177436f85622e51b2c15b98f0513d6c74f704aec05dcbc2f695a27cbae99d1
    HEAD_REF master
    PATCHES fix-msvc-crt.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_LIB=ON
        -DBUILD_TOOL=OFF
)

vcpkg_cmake_build()

file(INSTALL "${SOURCE_PATH}/include/par2"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# See note 2 above. Without this the headers fall back to different integer
# typedefs and every call fails to link.
file(GLOB _par2_config "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/config.h")
if(_par2_config)
    file(INSTALL ${_par2_config} DESTINATION "${CURRENT_PACKAGES_DIR}/include")
endif()

# par2-turbo, gf16 and hasher: all three are needed, and gf16/hasher come from
# include()d cmake files rather than subprojects, so they land beside the main
# archive rather than in subdirectories.
foreach(_cfg rel dbg)
    if(_cfg STREQUAL "rel")
        set(_dest "${CURRENT_PACKAGES_DIR}/lib")
    else()
        set(_dest "${CURRENT_PACKAGES_DIR}/debug/lib")
    endif()

    foreach(_name par2-turbo gf16 hasher)
        file(GLOB _found
            "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${_cfg}/${_name}.lib"
            "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${_cfg}/lib${_name}.a"
            "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${_cfg}/*/${_name}.lib"
            "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${_cfg}/*/lib${_name}.a")
        if(_found)
            file(INSTALL ${_found} DESTINATION "${_dest}")
        endif()
    endforeach()
endforeach()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")

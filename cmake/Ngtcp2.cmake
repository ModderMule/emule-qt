# ---------------------------------------------------------------------------
# ngtcp2 + ngtcp2_crypto_ossl — QUIC for the NAT-T transport (docs/protocol/natt-quic.md)
#
# Needs OpenSSL >= 3.5 (QUIC TLS API, SSL_set_quic_tls_cbs).
# An installed static ngtcp2 first (vcpkg, linux-deps, Homebrew), else v1.25.0
# built once at configure time into the build tree. Not FetchContent_MakeAvailable:
# ngtcp2's top-level project adds its own tests and a `check` target.
#
# Result: target emule::ngtcp2 and EMULE_QUIC_NATT left ON, or EMULE_QUIC_NATT OFF.
# ---------------------------------------------------------------------------
set(EMULE_NGTCP2_VERSION "1.25.0")

if(OPENSSL_VERSION VERSION_LESS "3.5")
    message(WARNING "OpenSSL ${OPENSSL_VERSION} has no QUIC TLS API (needs 3.5): QUIC NAT-T off")
    set(EMULE_QUIC_NATT OFF CACHE BOOL "" FORCE)
    return()
endif()

find_path(EMULE_NGTCP2_INCLUDE_DIR ngtcp2/ngtcp2_crypto_ossl.h)
find_library(EMULE_NGTCP2_LIBRARY NAMES ngtcp2_static libngtcp2.a ngtcp2)
find_library(EMULE_NGTCP2_OSSL_LIBRARY NAMES ngtcp2_crypto_ossl_static libngtcp2_crypto_ossl.a ngtcp2_crypto_ossl)

if(NOT EMULE_NGTCP2_INCLUDE_DIR OR NOT EMULE_NGTCP2_LIBRARY OR NOT EMULE_NGTCP2_OSSL_LIBRARY)
    set(_ng_root "${CMAKE_BINARY_DIR}/_deps/ngtcp2")
    set(_ng_prefix "${_ng_root}/install")
    if(NOT EXISTS "${_ng_prefix}/.built-${EMULE_NGTCP2_VERSION}")
        message(STATUS "ngtcp2: building v${EMULE_NGTCP2_VERSION} (static, OpenSSL backend)")
        include(FetchContent)
        FetchContent_Declare(ngtcp2_src
            GIT_REPOSITORY https://github.com/ngtcp2/ngtcp2.git
            GIT_TAG        v${EMULE_NGTCP2_VERSION}
            GIT_SHALLOW    TRUE
            SOURCE_DIR     "${_ng_root}/src"
            # no CMakeLists.txt there: MakeAvailable only downloads
            SOURCE_SUBDIR  emule-download-only
        )
        FetchContent_MakeAvailable(ngtcp2_src)
        set(_ng_args
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${_ng_prefix}
            -DCMAKE_INSTALL_LIBDIR=lib
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DENABLE_LIB_ONLY=ON
            -DENABLE_STATIC_LIB=ON
            -DENABLE_SHARED_LIB=OFF
            -DENABLE_OPENSSL=ON
            -DENABLE_GNUTLS=OFF
            -DBUILD_TESTING=OFF
        )
        foreach(_v CMAKE_C_COMPILER CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET
                   OPENSSL_ROOT_DIR OPENSSL_USE_STATIC_LIBS CMAKE_MSVC_RUNTIME_LIBRARY)
            if(DEFINED ${_v} AND NOT "${${_v}}" STREQUAL "")
                list(APPEND _ng_args "-D${_v}=${${_v}}")
            endif()
        endforeach()
        if(OPENSSL_INCLUDE_DIR)
            list(APPEND _ng_args "-DOPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}"
                                 "-DOPENSSL_SSL_LIBRARY=${OPENSSL_SSL_LIBRARY}"
                                 "-DOPENSSL_CRYPTO_LIBRARY=${OPENSSL_CRYPTO_LIBRARY}")
        endif()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -S "${_ng_root}/src" -B "${_ng_root}/build" ${_ng_args}
            RESULT_VARIABLE _ng_rc OUTPUT_QUIET)
        if(_ng_rc EQUAL 0)
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build "${_ng_root}/build" --config Release --target install --parallel
                RESULT_VARIABLE _ng_rc OUTPUT_QUIET)
        endif()
        if(NOT _ng_rc EQUAL 0)
            message(WARNING "ngtcp2 build failed: QUIC NAT-T off")
            set(EMULE_QUIC_NATT OFF CACHE BOOL "" FORCE)
            return()
        endif()
        file(WRITE "${_ng_prefix}/.built-${EMULE_NGTCP2_VERSION}" "")
    endif()
    unset(EMULE_NGTCP2_INCLUDE_DIR CACHE)
    unset(EMULE_NGTCP2_LIBRARY CACHE)
    unset(EMULE_NGTCP2_OSSL_LIBRARY CACHE)
    find_path(EMULE_NGTCP2_INCLUDE_DIR ngtcp2/ngtcp2_crypto_ossl.h PATHS "${_ng_prefix}/include" NO_DEFAULT_PATH)
    find_library(EMULE_NGTCP2_LIBRARY NAMES ngtcp2_static ngtcp2 PATHS "${_ng_prefix}/lib" NO_DEFAULT_PATH)
    find_library(EMULE_NGTCP2_OSSL_LIBRARY NAMES ngtcp2_crypto_ossl_static ngtcp2_crypto_ossl
                 PATHS "${_ng_prefix}/lib" NO_DEFAULT_PATH)
endif()

if(NOT EMULE_NGTCP2_INCLUDE_DIR OR NOT EMULE_NGTCP2_LIBRARY OR NOT EMULE_NGTCP2_OSSL_LIBRARY)
    message(WARNING "ngtcp2 not found: QUIC NAT-T off")
    set(EMULE_QUIC_NATT OFF CACHE BOOL "" FORCE)
    return()
endif()

message(STATUS "ngtcp2: ${EMULE_NGTCP2_LIBRARY}")
add_library(emule_ngtcp2 INTERFACE)
add_library(emule::ngtcp2 ALIAS emule_ngtcp2)
target_include_directories(emule_ngtcp2 SYSTEM INTERFACE "${EMULE_NGTCP2_INCLUDE_DIR}")
# crypto backend first: it needs symbols from libngtcp2
target_link_libraries(emule_ngtcp2 INTERFACE
    "${EMULE_NGTCP2_OSSL_LIBRARY}" "${EMULE_NGTCP2_LIBRARY}" OpenSSL::SSL OpenSSL::Crypto)
if(EMULE_NGTCP2_LIBRARY MATCHES "(\\.a|_static\\.lib)$")
    target_compile_definitions(emule_ngtcp2 INTERFACE NGTCP2_STATICLIB)
endif()
if(WIN32)
    target_link_libraries(emule_ngtcp2 INTERFACE ws2_32 bcrypt crypt32)
endif()

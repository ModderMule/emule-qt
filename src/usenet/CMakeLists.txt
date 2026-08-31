# ---------------------------------------------------------------------------
# libemuleusenet — Usenet (NNTP) download engine
#
# A peer of eMule::Core and eMule::Ipc, not a part of either.
#
# Dependency direction is one-way: usenet -> core is fine (Log, Preferences,
# ArchiveReader), core -> usenet must never happen.  Only emulecored links this;
# the GUI stays daemon-agnostic and talks IPC.
#
# See docs/UsenetModule-Research.local.md for the design this implements.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE USENET_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

add_library(emuleusenet STATIC ${USENET_SOURCES})
add_library(eMule::Usenet ALIAS emuleusenet)

target_include_directories(emuleusenet
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(emuleusenet
    PUBLIC
        Qt6::Core
        Qt6::Network
        OpenSSL::SSL
        OpenSSL::Crypto
        eMule::Core
        eMule::CommonSettings
)

if(EMULE_USENET_RAPIDYENC AND TARGET rapidyenc_static)
    target_link_libraries(emuleusenet PRIVATE rapidyenc_static)
    target_compile_definitions(emuleusenet PRIVATE EMULE_HAVE_RAPIDYENC=1)
endif()

# libpar2-turbo, PUBLIC on both counts.
#
# The definition, because the tests need EMULE_HAVE_PAR2 to know whether the
# repair cases can run. The library, because Par2Verifier's internal
# Par2Repairer subclass emits a vtable referencing Par2::Par2Repairer's typeinfo,
# and that has to resolve in whatever links this — a consumer of the static
# library, not the library itself. PRIVATE leaves every consumer with an
# undefined typeinfo at link time.
#
# It also hands the par2 headers to the tests, which build their own recovery
# sets with Par2::par2create rather than checking in binary fixtures.
if(EMULE_USENET_PAR2 AND TARGET emule_par2)
    target_link_libraries(emuleusenet PUBLIC emule_par2)
    target_compile_definitions(emuleusenet PUBLIC EMULE_HAVE_PAR2=1)

    # par2cmdline-turbo compiles itself with -fno-rtti (its cmake/common.cmake),
    # so its polymorphic classes ship a vtable and *no typeinfo*. Par2Verifier.cpp
    # subclasses Par2Repairer, and a subclass built with RTTI needs the base's
    # typeinfo to emit its own — which is not in the archive. The link then fails
    # in every consumer, not here, with "undefined typeinfo for
    # Par2::Par2Repairer" and no hint as to why.
    #
    # Match their flag for this one file. Nothing in it uses typeid or
    # dynamic_cast, and exception typeinfo is still emitted under -fno-rtti, so
    # the catch blocks are unaffected.
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/post/Par2Verifier.cpp"
        PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/GR-,-fno-rtti>")
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${USENET_SOURCES})

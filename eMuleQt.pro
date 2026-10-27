# eMuleQt — Top-level subdirs project
# Open this file in Qt Creator to browse / build the entire project.
# Prerequisite: run CMake once so that build/generated/config.h exists
# and third-party deps (miniupnpc, yaml-cpp, libarchive) are available.

TEMPLATE = subdirs

SUBDIRS = \
    core \
    ipc \
    indexer \
    usenet \
    bt \
    daemon \
    gui \
    tests

# ed3k (packmule): only with libs/packmule checked out, as CMake EMULE_ED3K
exists($$PWD/libs/packmule/Cargo.toml) {
    SUBDIRS += ed3k
    ed3k.subdir  = src/ed3k
    ed3k.depends = core usenet bt
    daemon.depends += ed3k
}

core.subdir    = src/core
ipc.subdir     = src/ipc
indexer.subdir = src/indexer
usenet.subdir  = src/usenet
bt.subdir      = src/bt
daemon.subdir  = src/daemon
gui.subdir     = src/gui

indexer.depends = core
usenet.depends  = core
bt.depends      = core
daemon.depends  = core ipc indexer usenet bt
gui.depends     = core ipc
tests.depends   = core ipc indexer usenet

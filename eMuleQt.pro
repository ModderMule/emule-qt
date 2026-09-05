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
    daemon \
    gui \
    tests

core.subdir    = src/core
ipc.subdir     = src/ipc
indexer.subdir = src/indexer
usenet.subdir  = src/usenet
daemon.subdir  = src/daemon
gui.subdir     = src/gui

indexer.depends = core
usenet.depends  = core
daemon.depends  = core ipc indexer usenet
gui.depends     = core ipc
tests.depends   = core ipc indexer usenet

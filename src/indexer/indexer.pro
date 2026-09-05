# emuleindexer — the shared newznab / torznab search client

TEMPLATE = lib
CONFIG  += staticlib c++2b
TARGET   = emuleindexer

QT += core network
QT -= gui

INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../core

SOURCES += \
    IndexerCaps.cpp \
    IndexerCapsStore.cpp \
    IndexerClient.cpp \
    IndexerQuery.cpp \
    IndexerResult.cpp \
    IndexerSearch.cpp \
    IndexerSearchList.cpp

HEADERS += \
    IndexerCaps.h \
    IndexerCapsStore.h \
    IndexerClient.h \
    IndexerConfig.h \
    IndexerQuery.h \
    IndexerResult.h \
    IndexerSearch.h \
    IndexerSearchList.h

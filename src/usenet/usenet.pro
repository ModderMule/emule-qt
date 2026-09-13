# emuleusenet — Usenet (NNTP) static library

TEMPLATE = lib
CONFIG  += staticlib c++2b
TARGET   = emuleusenet

QT += core network
QT -= gui

INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../core

# OpenSSL
unix:  LIBS += -lssl -lcrypto
win32 {
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN

    OPENSSL_DIR = $$(OPENSSL_DIR)
    isEmpty(OPENSSL_DIR): OPENSSL_DIR = "C:/Program Files/OpenSSL-Win64"
    INCLUDEPATH += "$$OPENSSL_DIR/include"
    LIBS += -L"$$OPENSSL_DIR/lib" -lssl -lcrypto
}

SOURCES += \
    UsenetSession.cpp \
    decode/YencDecoder.cpp \
    nntp/ArticleFetcher.cpp \
    nntp/NntpCommand.cpp \
    nntp/NntpError.cpp \
    nntp/NntpServerPool.cpp \
    nntp/NntpSocket.cpp \
    nzb/NzbFile.cpp \
    nzb/NzbUrlFetch.cpp \
    nzb/NzbInfo.cpp \
    nzb/SubjectParser.cpp \
    post/Par2NameIndex.cpp \
    post/Par2Verifier.cpp \
    post/UsenetDirectUnpack.cpp \
    post/UsenetPostProcessor.cpp \
    post/UsenetUnpacker.cpp \
    queue/ArticleWriter.cpp \
    queue/UsenetQueue.cpp \
    queue/UsenetWatchFolder.cpp \
    queue/UsenetQueueItem.cpp \
    queue/UsenetQueueStore.cpp \
    queue/UsenetHealth.cpp \
    queue/UsenetHistory.cpp \
    queue/UsenetStatistics.cpp \
    queue/UsenetUsage.cpp \
    queue/UsenetWorker.cpp \
    stream/RarReader.cpp \
    stream/UsenetEncryptedPreview.cpp \
    stream/UsenetStreamIndex.cpp

HEADERS += \
    UsenetSession.h \
    decode/YencDecoder.h \
    nntp/ArticleFetcher.h \
    nntp/NewsServer.h \
    nntp/NntpCommand.h \
    nntp/NntpError.h \
    nntp/NntpServerPool.h \
    nntp/NntpSocket.h \
    nzb/NzbFile.h \
    nzb/NzbUrlFetch.h \
    nzb/NzbInfo.h \
    nzb/SubjectParser.h \
    post/Par2NameIndex.h \
    post/Par2Verifier.h \
    post/UsenetDirectUnpack.h \
    post/UsenetPostProcessor.h \
    post/UsenetUnpacker.h \
    queue/ArticleWriter.h \
    queue/UsenetQueue.h \
    queue/UsenetWatchFolder.h \
    queue/UsenetQueueItem.h \
    queue/UsenetQueueStore.h \
    queue/UsenetHealth.h \
    queue/UsenetHistory.h \
    queue/UsenetStatistics.h \
    queue/UsenetUsage.h \
    queue/UsenetWorker.h \
    stream/RarReader.h \
    stream/UsenetEncryptedPreview.h \
    stream/UsenetStreamIndex.h

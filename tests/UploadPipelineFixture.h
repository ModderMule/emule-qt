#pragma once

/// @file UploadPipelineFixture.h
/// @brief The uploader half of a running eMule, stood up in-process.
///
/// A real ListenSocket on an ephemeral port, a real UploadQueue with a real disk
/// IO thread, and a shared file list — enough that a mock peer can connect over
/// loopback, complete an ed2k handshake and be granted an upload slot. Tests that
/// need to observe what our upload side *decides* build on this rather than on
/// hand-made UpDownClients, because most of the decisions live in the queue.
///
/// No Q_OBJECT: it owns a QTimer and some pointers, and declares no signals or
/// slots of its own.

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "crypto/MD4Hash.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "net/EMSocket.h"
#include "net/ListenSocket.h"
#include "prefs/Preferences.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "transfer/UploadQueue.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <array>
#include <vector>

namespace eMule::testing {

class UploadPipelineFixture {
public:
    /// Build everything and start the process timer. Uses QVERIFY, so a failure
    /// here marks the calling test failed the same way an inline setup would.
    void setup(QObject* parent)
    {
        tmpDir = new TempDir();

        // 1. Preferences
        thePrefs.load(tmpDir->filePath(QStringLiteral("prefs.yaml")));
        thePrefs.setConfigDir(tmpDir->path());
        thePrefs.setCryptLayerSupported(true);
        thePrefs.setCryptLayerRequested(true);
        thePrefs.setCryptLayerRequired(false);

        const QString incomingDir = tmpDir->filePath(QStringLiteral("incoming"));
        QDir().mkpath(incomingDir);
        thePrefs.setIncomingDir(incomingDir);
        thePrefs.setTempDirs({tmpDir->filePath(QStringLiteral("temp"))});

        // 2. Client credits (generates RSA keys → user hash)
        auto* creditsList = new ClientCreditsList();
        theApp.clientCredits = creditsList;

        // 3. Client list
        clientList = new ClientList(parent);
        theApp.clientList = clientList;

        // 4. Listen socket (port 0 = random)
        listenSocket = new ListenSocket(parent);
        QVERIFY2(listenSocket->startListening(0), "Failed to start TCP listener");
        theApp.listenSocket = listenSocket;
        thePrefs.setPort(listenSocket->connectedPort());

        // Wire incoming connections
        QObject::connect(listenSocket, &ListenSocket::newClientConnection,
                         clientList, &ClientList::handleIncomingConnection);

        // 5. Upload bandwidth throttler — NOT set on the UploadQueue and NOT
        //    published through theApp. Qt 6 forbids QTcpSocket writes from
        //    non-owner threads, but the throttler thread calls
        //    sendFileAndControlData() from its own thread. Instead, we flush
        //    standard/file data packets from the main thread in the process timer.
        throttler = new UploadBandwidthThrottler(parent);

        // 6. Known file list
        knownFiles = new KnownFileList();
        theApp.knownFileList = knownFiles;

        // 7. Shared file list
        sharedFiles = new SharedFileList(knownFiles, parent);
        theApp.sharedFileList = sharedFiles;

        // 8. Upload disk IO thread
        diskIO = new UploadDiskIOThread(parent);
        diskIO->start();

        // 9. Upload queue — throttler intentionally NOT set (see note at step 5)
        uploadQueue = new UploadQueue(parent);
        uploadQueue->setDiskIOThread(diskIO);
        uploadQueue->setSharedFileList(sharedFiles);
        theApp.uploadQueue = uploadQueue;

        // 10. Process timer — drives upload queue and listen socket.
        //     Also flushes file data packets from the main thread because
        //     Qt 6 sockets don't support writes from non-owner threads
        //     (the UploadBandwidthThrottler thread).
        QObject::connect(&processTimer, &QTimer::timeout, &processTimer, [this] {
            uploadQueue->process();
            listenSocket->process();
            // Flush any pending file data from the main thread
            uploadQueue->forEachUploading([](UpDownClient* client) {
                if (auto* sock = client->socket())
                    sock->sendFileAndControlData(UINT32_MAX, 1);
            });
        });
        processTimer.start(100);
    }

    void teardown()
    {
        processTimer.stop();

        if (listenSocket)
            listenSocket->stopListening();

        if (diskIO) {
            diskIO->endThread();
            diskIO->wait(5000);
        }

        // Throttler was never handed to the queue — no need to endThread/wait

        // Reset all globals
        theApp.uploadQueue = nullptr;
        theApp.sharedFileList = nullptr;
        theApp.knownFileList = nullptr;
        theApp.clientList = nullptr;
        theApp.listenSocket = nullptr;
        theApp.uploadBandwidthThrottler = nullptr;
        delete theApp.clientCredits;
        theApp.clientCredits = nullptr;

        delete knownFiles;
        knownFiles = nullptr;

        delete tmpDir;
        tmpDir = nullptr;
    }

    /// Hash a real file the way eMule does — per-part MD4, then MD4 over the part
    /// hashes — and publish it as a shared file.
    void registerSharedFile(const QString& path, std::array<uint8, 16>& outHash, uint64& outSize)
    {
        QVERIFY2(QFile::exists(path),
                 qPrintable(QStringLiteral("Test file not found: %1").arg(path)));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        outSize = static_cast<uint64>(f.size());
        QVERIFY(outSize > 0);

        const uint64 partSize = PARTSIZE;
        const uint64 numParts = (outSize + partSize - 1) / partSize;
        std::vector<std::array<uint8, 16>> partHashes;

        for (uint64 p = 0; p < numParts; ++p) {
            const uint64 offset = p * partSize;
            const uint64 remaining = outSize - offset;
            const auto chunkSize = static_cast<qint64>(std::min(remaining, partSize));

            QByteArray chunk = f.read(chunkSize);
            QCOMPARE(chunk.size(), chunkSize);

            MD4Hasher hasher;
            hasher.add(chunk.constData(), static_cast<std::size_t>(chunk.size()));
            hasher.finish();

            std::array<uint8, 16> h{};
            std::memcpy(h.data(), hasher.getHash(), 16);
            partHashes.push_back(h);
        }

        if (numParts == 1) {
            std::memcpy(outHash.data(), partHashes[0].data(), 16);
        } else {
            MD4Hasher fileHasher;
            for (const auto& ph : partHashes)
                fileHasher.add(ph.data(), 16);
            fileHasher.finish();
            std::memcpy(outHash.data(), fileHasher.getHash(), 16);
        }

        auto* knownFile = new KnownFile();
        knownFile->setFileName(QFileInfo(path).fileName());
        knownFile->setFileSize(EMFileSize(outSize));
        knownFile->setFileHash(outHash.data());
        knownFile->setFilePath(path);
        knownFile->setPath(QFileInfo(path).absolutePath() + QDir::separator());
        knownFile->fileIdentifier().setMD4HashSet(partHashes);

        QVERIFY(knownFiles->safeAddKFile(knownFile));
        QVERIFY(sharedFiles->safeAddKFile(knownFile));
        QVERIFY(sharedFiles->getFileByID(outHash.data()) != nullptr);
    }

    TempDir* tmpDir = nullptr;
    ListenSocket* listenSocket = nullptr;
    ClientList* clientList = nullptr;
    UploadBandwidthThrottler* throttler = nullptr;
    KnownFileList* knownFiles = nullptr;
    SharedFileList* sharedFiles = nullptr;
    UploadQueue* uploadQueue = nullptr;
    UploadDiskIOThread* diskIO = nullptr;
    QTimer processTimer;
};

} // namespace eMule::testing

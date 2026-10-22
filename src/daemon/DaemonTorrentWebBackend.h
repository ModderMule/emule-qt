#pragma once

/// @file DaemonTorrentWebBackend.h
/// @brief The REST API's view of the BitTorrent engine, as the daemon provides it.
///
/// Every call goes through BtBridge::handleRequest, the IPC's one implementation, so
/// the GUI and /api/v1/torrents cannot drift apart. Only a BitTorrent build has it.

#ifdef EMULE_HAVE_BITTORRENT

#include "IpcProtocol.h"

#include "webserver/TorrentWebBackend.h"

#include <QCborValue>

namespace eMule {

class DaemonTorrentWebBackend final : public TorrentWebBackend {
public:
    DaemonTorrentWebBackend() = default;

    [[nodiscard]] bool running() const override;
    [[nodiscard]] QCborMap status() const override;
    [[nodiscard]] QCborArray torrents() const override;
    [[nodiscard]] QCborMap torrent(const QString& id) const override;
    [[nodiscard]] QCborMap details(const QString& id) const override;

    TorrentWebAddResult addTorrentFile(const QByteArray& data, const QString& savePath,
                                       bool paused) override;
    TorrentWebAddResult addMagnet(const QString& uri, const QString& savePath, bool paused) override;

    bool remove(const QString& id, bool deleteFiles) override;
    bool pause(const QString& id) override;
    bool resume(const QString& id) override;
    bool recheck(const QString& id) override;
    bool reannounce(const QString& id) override;

    bool setFilePriorities(const QString& id, const QList<int>& priorities) override;
    bool setOptions(const QString& id, const QCborMap& options) override;
    bool addTrackers(const QString& id, const QStringList& urls) override;
    bool removeTrackers(const QString& id, const QStringList& urls) override;

private:
    /// The request answered in place: the data of a successful Result, else undefined.
    [[nodiscard]] static QCborValue ask(Ipc::IpcMsgType type, const QCborArray& fields);
    [[nodiscard]] static bool askBool(Ipc::IpcMsgType type, const QCborArray& fields);
    [[nodiscard]] static TorrentWebAddResult addResult(const QCborValue& reply);
};

} // namespace eMule

#endif // EMULE_HAVE_BITTORRENT

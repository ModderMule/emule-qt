#pragma once

/// @file MetaResultActions.h
/// @brief Actions on eNode torrent/Usenet/web/container search rows, including the login detour.
///
/// Usenet "Download" queues the NZB daemon-side (DownloadMetaResult); "Download
/// NZB File", "Download Torrent" and "Download Container" fetch the metafile and
/// save it here; a web row's links open in WebLinksDialog, and its Download hands
/// them to the Hoster tab. A torrent's Download fetches the .torrent for the
/// Torrents tab, a magnet-only one hands its magnet there; with BitTorrent off or
/// not built the .torrent is saved and the magnet copied, for another client.
/// Any fetch may answer
/// AuthRequired / AccountInactive: one MetaAccountDialog per server opens, and
/// every request that hit it is retried once it accepts.

#include <QCborMap>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QWidget;

namespace eMule {

class IpcClient;
class MetaAccountDialog;
class SearchResultsModel;

class MetaResultActions : public QObject {
    Q_OBJECT

public:
    /// One row, as the panel knows it.
    struct Row {
        QString hash;
        QString name;
        int kind = 0;   ///< enodemeta::Kind
        QCborMap ref;   ///< SearchResultRow::metaRef(), for a row whose search is gone
        QString magnet;

        [[nodiscard]] bool isNzb() const { return kind == 3; }
        [[nodiscard]] bool isContainer() const { return kind == 6; }
    };

    MetaResultActions(IpcClient* ipc, QWidget* parentWidget);

    /// Queue Usenet rows in eMuleQt's Usenet downloader.
    void downloadUsenet(quint32 searchID, const QList<Row>& rows, int category,
                        QPointer<SearchResultsModel> model);

    /// Save the .torrent/.nzb/.dlc behind @p rows: a file dialog for one, a folder for several.
    void saveMetaFiles(quint32 searchID, const QList<Row>& rows);

    /// Fetch each web row's links and show them, a dialog a row.
    void showWebLinks(quint32 searchID, const QList<Row>& rows);

    /// Fetch each web row's links and start them in the Hoster tab, one download a
    /// file with its mirrors; links shown instead while hoster downloads are off.
    void downloadWebLinks(quint32 searchID, const QList<Row>& rows);

    /// Magnet-only torrents: their magnets to the clipboard, one a line.
    static void copyMagnets(const QList<Row>& rows);

    /// Magnet-only torrents' Download: to the Torrents tab, else copyMagnets.
    void handOverMagnets(const QList<Row>& rows);

    /// Torrents with a .torrent: fetch each for the Torrents tab (its magnet when
    /// the fetch fails), else saveMetaFiles.
    void addTorrents(quint32 searchID, const QList<Row>& rows);

    /// Account status / log out for a server (server list).
    void manageAccount(const QString& serverAddr, const QString& serverName);

private:
    void sendDownload(quint32 searchID, const Row& row, int category, bool force,
                      QPointer<SearchResultsModel> model);
    /// FetchMetaFile for @p row, login detour included. @p onData gets the reply's
    /// map; a failure warns with @p failText (%1 name, %2 reason) unless @p onFail
    /// handles it.
    void fetchMeta(quint32 searchID, const Row& row, const QString& failText,
                   std::function<void(const QCborMap& data)> onData, std::function<bool()> onFail = {});
    void fetchAndSave(quint32 searchID, const Row& row, const QString& path, bool pathIsDir);
    void fetchAndShowLinks(quint32 searchID, const Row& row);
    void fetchAndDownloadLinks(quint32 searchID, const Row& row);
    void fetchAndAddTorrent(quint32 searchID, const Row& row);

    /// Handle an auth failure: open (or reuse) the server's dialog, retry on accept.
    /// Returns false when @p meta is no auth problem.
    bool handleAuth(const QCborMap& meta, std::function<void()> retry);
    void warn(const QString& text);

    IpcClient* m_ipc;
    QPointer<QWidget> m_parent;
    QHash<QString, QPointer<MetaAccountDialog>> m_dialogs;
    QHash<QString, QList<std::function<void()>>> m_retries;
};

} // namespace eMule

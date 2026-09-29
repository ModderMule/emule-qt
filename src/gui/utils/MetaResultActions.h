#pragma once

/// @file MetaResultActions.h
/// @brief Actions on eNode torrent/Usenet search rows, including the login detour.
///
/// Usenet "Download" queues the NZB daemon-side (DownloadMetaResult); "Download
/// NZB File" and "Download Torrent" fetch the metafile and save it here. Any of
/// them may answer AuthRequired / AccountInactive: one MetaAccountDialog per
/// server opens, and every request that hit it is retried once it accepts.

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
        bool nzb = false;
        QCborMap ref;   ///< SearchResultRow::metaRef(), for a row whose search is gone
    };

    MetaResultActions(IpcClient* ipc, QWidget* parentWidget);

    /// Queue Usenet rows in eMuleQt's Usenet downloader.
    void downloadUsenet(quint32 searchID, const QList<Row>& rows, int category,
                        QPointer<SearchResultsModel> model);

    /// Save the .torrent/.nzb behind @p rows: a file dialog for one, a folder for several.
    void saveMetaFiles(quint32 searchID, const QList<Row>& rows);

    /// Account status / log out for a server (server list).
    void manageAccount(const QString& serverAddr, const QString& serverName);

private:
    void sendDownload(quint32 searchID, const Row& row, int category, bool force,
                      QPointer<SearchResultsModel> model);
    void fetchAndSave(quint32 searchID, const Row& row, const QString& path, bool pathIsDir);

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

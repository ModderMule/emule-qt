#pragma once

/// @file MetaResultDetailDialog.h
/// @brief Details of a torrent or Usenet search result: the fetched .torrent/.nzb
///        shown the way a download would show it (General, Files, Trackers).
///
/// Opened from eNode torrent/NZB rows of an eD2K search and from indexer
/// (Newznab/Torznab) rows. The daemon fetches and parses the file
/// (FetchMetaFile with inspect, InspectIndexerResult) and keeps it 3 h, so the
/// Download button here does not fetch it again. A magnet-only row never gets
/// here: there is no file to show. Modeless, like UsenetDetailsDialog.

#include "DetailDialog.h"

#include <QCborMap>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

class QFormLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTreeWidget;

namespace eMule {

class ArchiveContentsView;
class PreviewImageView;
class PreviewNfoView;
class UsenetFileCheckList;

class MetaResultDetailDialog : public DetailDialog {
    Q_OBJECT

public:
    enum Tab { TabGeneral = 0, TabFiles, TabTrackers, TabImages, TabNfo, TabArchive };
    enum NzbColumn { NzbName = 0, NzbSize, NzbArticles, NzbType, NzbColCount };
    enum TorrentColumn { TorrentName = 0, TorrentSize, TorrentColCount };

    /// What the search row already says, shown above the file's own facts.
    struct RowInfo {
        QString name;
        bool torrent = false;
        qint64 size = -1;
        QString source;     ///< eNode server or indexer name
        int seeders = -1;
        int peers = -1;
        int ageDays = -1;
        QString category;
        /// Check boxes pick the files Download fetches (NZB and torrent rows alike).
        bool chooseFiles = false;
        /// With chooseFiles: pre-check only the file the row stands for, when the
        /// daemon names one (rowFileIndex). NZB: usenetMetaSelectedFileOnly; torrent: btMetaSelectedFileOnly.
        bool selectedFileOnly = false;
    };

    explicit MetaResultDetailDialog(const RowInfo& row, QWidget* parent = nullptr);

    /// {kind: "torrent" | "nzb", inspection} from the daemon, or {error}. Public for tests.
    void setDetails(const QCborMap& details) override;
    /// The fetch failed.
    void setError(const QString& error);

    [[nodiscard]] QTabWidget* tabs() const { return m_tabs; }
    [[nodiscard]] QTreeWidget* filesTree() const { return m_files; }
    [[nodiscard]] QTreeWidget* trackersTree() const { return m_trackers; }
    [[nodiscard]] QLabel* noteLabel() const { return m_note; }
    [[nodiscard]] QPushButton* downloadButton() const { return m_download; }
    [[nodiscard]] UsenetFileCheckList* fileChecks() const { return m_checks; }
    [[nodiscard]] PreviewImageView* imagesView() const { return m_images; }
    [[nodiscard]] PreviewNfoView* nfoView() const { return m_nfo; }
    /// Details › Archive (hoster.md §6.1); its tab shows when the file lists archives.
    [[nodiscard]] ArchiveContentsView* archiveView() const { return m_archive; }
    /// LookupArchiveContents kind and payload for the file shown; empty kind: none.
    [[nodiscard]] std::pair<QString, QCborMap> archiveRequest() const { return m_archiveRequest; }

    /// FetchNzbImages answer: {images: [{name, kind, data}], note}.
    void setPreviewImages(const QCborMap& result);
    void setPreviewImagesError(const QString& error);
    /// FetchNzbNfo answer: {nfos: [{name, text}], note}.
    void setPreviewNfo(const QCborMap& result);
    void setPreviewNfoError(const QString& error);

signals:
    /// Download pressed: the panel adds the row as its own Download would.
    /// @p unchecked: NZB file indices left out (chooseFiles), else empty.
    /// @p kept: the checked files (NZB without PAR2, or torrent); empty = all.
    /// @p entries, @p password: archive entries to extract (Archive tab), else empty.
    void downloadRequested(const QList<int>& unchecked, const QList<int>& kept,
                           const QHash<QString, QStringList>& entries, const QString& password);
    /// The Images tab was opened the first time. Shown only for an NZB whose
    /// inspection says hasImages, or with a cover URL.
    void imagesRequested(const QString& previewToken, const QString& coverUrl);
    /// The NFO tab was opened the first time; shown only when the NZB lists an .nfo.
    void nfoRequested(const QString& previewToken);

private:
    void buildGeneral(const QCborMap& inspection, bool torrent);
    void buildTorrentFiles(const QCborMap& inspection, int rowFileIndex);
    void buildNzbFiles(const QCborMap& inspection, int rowFileIndex);
    void buildTrackers(const QCborMap& inspection);
    void addRowFacts(QFormLayout* form) const;
    void onDownload();
    void download(const QHash<QString, QStringList>& entries, const QString& password);
    void onTabChanged(int index);

    RowInfo m_row;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_note = nullptr;
    QTabWidget* m_tabs = nullptr;
    QWidget* m_general = nullptr;
    QTreeWidget* m_files = nullptr;
    QTreeWidget* m_trackers = nullptr;
    QPushButton* m_download = nullptr;
    UsenetFileCheckList* m_checks = nullptr;
    PreviewImageView* m_images = nullptr;
    PreviewNfoView* m_nfo = nullptr;
    ArchiveContentsView* m_archive = nullptr;
    std::pair<QString, QCborMap> m_archiveRequest;
    QString m_previewToken;
    QString m_coverUrl;
    bool m_imagesRequested = false;
    bool m_nfoRequested = false;
};

} // namespace eMule

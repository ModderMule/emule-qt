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
#include <QString>

class QFormLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTreeWidget;

namespace eMule {

class MetaResultDetailDialog : public DetailDialog {
    Q_OBJECT

public:
    enum Tab { TabGeneral = 0, TabFiles, TabTrackers };
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

signals:
    /// Download pressed: the panel adds the row as its own Download would.
    void downloadRequested();

private:
    void buildGeneral(const QCborMap& inspection, bool torrent);
    void buildTorrentFiles(const QCborMap& inspection);
    void buildNzbFiles(const QCborMap& inspection);
    void buildTrackers(const QCborMap& inspection);
    void addRowFacts(QFormLayout* form) const;

    RowInfo m_row;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_note = nullptr;
    QTabWidget* m_tabs = nullptr;
    QWidget* m_general = nullptr;
    QTreeWidget* m_files = nullptr;
    QTreeWidget* m_trackers = nullptr;
    QPushButton* m_download = nullptr;
};

} // namespace eMule

#pragma once

/// @file SearchResultsModel.h
/// @brief Tree model for search results in the Search window.
///
/// Top-level rows are files. A file found under more than one name has one child
/// row per name, as MFC's list does (srchybrid/SearchListCtrl.cpp:1151-1210).

#include <QCborArray>
#include <QCborMap>
#include <QHash>
#include <QString>

#include <QAbstractItemModel>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace eMule {

/// One name a file was found under — a child row of its SearchResultRow.
struct SearchChildRow {
    QString fileName;
    QString directory;
    QString aichHash;
    QString artist;
    QString album;
    QString title;
    QString codec;
    int64_t sourceCount = 0;   ///< sources that reported this name
    int64_t length = 0;
    int64_t bitrate = 0;
};

/// Row data for one search result (SearchFile) in the results list.
struct SearchResultRow {
    QString hash;
    QString fileName;
    QString fileType;
    QString artist;
    QString album;
    QString title;
    QString codec;
    int64_t fileSize = 0;
    int64_t sourceCount = 0;
    int64_t completeSourceCount = 0;
    int64_t length = 0;
    int64_t bitrate = 0;
    int knownType = 0;
    bool seenBefore = false;    ///< on record before this search (daemon seen-files index)
    int seenNames = 0;          ///< names it has been seen under
    int64_t firstSeen = 0;      ///< unix seconds; 0 = not on record
    bool isKad = false;         ///< Kad results carry no complete-source count
    bool kadOrigin = false;     ///< server result the server found on Kad: Kad badge
    bool inDirectory = false;   ///< from browsing a client's shared files
    bool isSpam = false;
    bool hasComment = false;
    bool previewPossible = false;   ///< a browsed peer can send preview frames
    int userRating = 0;   // wire value: 6 means a Kad note lookup is running

    // Fake-file verdict (ids from the daemon; empty confidence = not judged)
    QString confidence;
    int fakeScore = 0;
    QStringList fakeReasons;

    // eNode meta row — network from the meta hash (enodemeta::Kind), 0 = eD2K
    int metaKind = 0;
    QString magnet;       ///< torrents only, may be empty
    int64_t metaAgeDays = 0;
    QString metaIndexer;
    QString metaCatalogId;   ///< FT_META_ID, echoed on the metafile fetch
    QCborArray metaServers;  ///< answering servers, [[ip, port], …]

    QString directory;       ///< folder of a browsed peer's file
    QString aichHash;        ///< empty when the answers gave none, or disagreed
    int kadPublishers = 0;   ///< Kad results: publishers behind the count
    int clientCount = 0;     ///< server results: clients named as sources
    std::vector<SearchChildRow> children;   ///< empty unless found under several names

    /// Model-assigned identity, never 0. Child indexes carry it as internalId.
    quintptr uid = 0;

    [[nodiscard]] bool isMeta() const { return metaKind != 0; }
    [[nodiscard]] bool isTorrent() const { return metaKind == 1 || metaKind == 2; }
    [[nodiscard]] bool isUsenet() const { return metaKind == 3; }

    /// ed2k:// link; empty for meta rows (their hash is not an MD4).
    /// @p name stands in for the file's own name (a name row).
    [[nodiscard]] QString ed2kLink(const QString& name = {}) const;
    /// urn:ed2k magnet, or the server's magnet for a meta row (empty for Usenet).
    [[nodiscard]] QString magnetLink(const QString& name = {}) const;
    /// What the daemon needs to fetch a meta row whose search is gone (a restored tab).
    [[nodiscard]] QCborMap metaRef() const;
};

/// A row of the list as the panel acts on it: the file, and the name it was picked
/// under when the row is a child.
struct SearchResultRef {
    const SearchResultRow* row = nullptr;
    const SearchChildRow* child = nullptr;

    explicit operator bool() const { return row != nullptr; }
    /// The child's name on a child row, the file's own otherwise.
    [[nodiscard]] const QString& fileName() const { return child ? child->fileName : row->fileName; }
};

/// Tree model backing the search results view in the Search panel.
class SearchResultsModel : public QAbstractItemModel {
    Q_OBJECT

public:
    /// True on a child (alternative name) row.
    static constexpr int ChildRole = Qt::UserRole + 1;
    /// True on a file MFC sorts to the bottom: spam, with the spam filter on.
    static constexpr int SpamRole  = Qt::UserRole + 2;

    enum Column {
        ColFileName = 0,
        ColSize,
        ColAvailability,
        ColConfidence,   ///< fake-file verdict, beside the count it qualifies
        ColComplete,
        ColType,
        ColArtist,
        ColAlbum,
        ColTitle,
        ColLength,
        ColBitrate,
        ColCodec,
        ColKnown,
        ColSeen,
        // MFC's hidden-by-default columns; appended so saved layouts keep their indexes
        ColFileID,
        ColFolder,
        ColAichHash,
        ColCount
    };

    explicit SearchResultsModel(QObject* parent = nullptr);

    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& index) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Take a new snapshot. Rows are matched by hash and children by name, so the
    /// view keeps its selection and its expanded files across a refresh.
    void setResults(std::vector<SearchResultRow> results);

    /// Remove a single file by top-level row.
    void removeRow(int row);
    /// One name row of a file; the file and its other names stay.
    void removeChild(int row, int childRow);

    /// Number of files (child rows not counted).
    [[nodiscard]] int resultCount() const { return static_cast<int>(m_rows.size()); }

    /// Get the file hash for a top-level row.
    [[nodiscard]] QString hashAt(int row) const;

    /// The file at a top-level row (nullptr if out of range).
    [[nodiscard]] const SearchResultRow* resultAt(int row) const;

    /// The file behind an index of this model — its own row or one of its names.
    [[nodiscard]] SearchResultRef resultAt(const QModelIndex& index) const;

    /// "12.03.25 · 3 names" for a file met before this search; empty otherwise.
    [[nodiscard]] static QString seenText(const SearchResultRow& r);

    /// Update the knownType for a specific row (triggers dataChanged).
    void setKnownType(int row, int knownType);

    /// Batch-update knownType by hash. Map: hash → knownType.
    void updateKnownTypes(const QHash<QString, int>& typesByHash);

private:
    [[nodiscard]] QVariant childData(const SearchResultRow& r, const SearchChildRow& c,
                                     int column, int role) const;
    [[nodiscard]] QVariant foreground(const SearchResultRow& r, int64_t sources, int column,
                                      bool child) const;
    void setChildren(int row, std::vector<SearchChildRow> children);
    void updateHasMeta();
    [[nodiscard]] int rowOfUid(quintptr uid) const { return m_rowByUid.value(uid, -1); }
    void reindexRows();

    std::vector<SearchResultRow> m_rows;
    QHash<quintptr, int> m_rowByUid;
    quintptr m_nextUid = 1;

    /// Any torrent/Usenet row: eD2K rows then reserve the network badge slot.
    bool m_hasMeta = false;
};

} // namespace eMule

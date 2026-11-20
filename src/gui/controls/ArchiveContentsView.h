#pragma once

/// @file ArchiveContentsView.h
/// @brief Details › Archive: what each archive of a release holds, before downloading it
/// (hoster.md §6.1). The core looks the contents up in ed3k (LookupArchiveContents); the
/// user checks entries and downloads them from ed3k by root, or downloads the archive and
/// extracts only the checked entries. Shared by search Details, web rows and Transfers.

#include <QCborArray>
#include <QCborMap>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class ArchiveContentsView : public QWidget {
    Q_OBJECT

public:
    enum Column { ColName = 0, ColSize, ColStatus, ColCount };

    /// @p archiveFallback: offer "Download archive, extract checked" (search rows).
    explicit ArchiveContentsView(QWidget* parent = nullptr, bool archiveFallback = true);

    /// A queued download (Transfers): the archive button sets what its unpack extracts; the
    /// answer's kept lists preset the check boxes, editable: false disables the button.
    void setQueued(bool queued);
    /// Starts the lookup once (the tab was opened); later calls do nothing.
    void ensureLookup();
    /// Forgets the answer: the next ensureLookup() asks again (another item shown).
    void reset();
    /// LookupArchiveContents answer: {archives: [...]}.
    void setResult(const QCborMap& result);
    void setError(const QString& error);
    /// The answer of a nested lookup asked by nestedLookupRequested(@p rootHex).
    void setNestedResult(const QString& rootHex, const QCborMap& result);
    void setNestedError(const QString& rootHex, const QString& error);

    /// Checked entries found in ed3k: [{root, size, path}] (paths of nested archives below theirs).
    [[nodiscard]] QCborArray checkedEntries() const;
    /// Checked entries per top archive: first volume's name, lower case → paths.
    /// Archives with every entry checked are left out (all extracted anyway).
    [[nodiscard]] QHash<QString, QStringList> checkedByArchive() const;
    /// The password a record carried, the first one; empty: none.
    [[nodiscard]] QString password() const;
    [[nodiscard]] QTreeWidget* tree() const { return m_tree; }
    [[nodiscard]] QLabel* statusLabel() const { return m_status; }
    [[nodiscard]] QPushButton* downloadEntriesButton() const { return m_downloadEntries; }
    [[nodiscard]] QPushButton* downloadArchiveButton() const { return m_downloadArchive; }
    [[nodiscard]] QPushButton* takeFromDiskButton() const { return m_takeFromDisk; }
    /// #224: the archives whose every entry is on disk here, as TakeArchiveFromDisk takes them:
    /// [{name, parts, entries: [{root, size, path}]}].
    [[nodiscard]] QCborArray localArchives() const;
    /// A file name that opens an archive (its first volume, or a lone archive).
    [[nodiscard]] static bool opensArchive(const QString& path);

signals:
    void lookupRequested();
    /// The lookup ended: archives found listed, -1 when it failed.
    void lookupFinished(int found);
    void nestedLookupRequested(const QString& name, const QString& rootHex);
    void downloadEntriesRequested(const QCborArray& entries);
    void downloadArchiveRequested(const QHash<QString, QStringList>& entries, const QString& password);
    /// #224: "Take From Disk" of a queued download: localArchives().
    void takeFromDiskRequested(const QCborArray& archives);

private:
    void lookUp();
    void fillArchive(QTreeWidgetItem* top, const QCborMap& archive);
    void updateButtons();
    void showMenu(const QPoint& pos);
    [[nodiscard]] static QString stateText(const QCborMap& archive);

    QLabel* m_status = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_again = nullptr;
    QPushButton* m_downloadEntries = nullptr;
    QPushButton* m_downloadArchive = nullptr;
    QPushButton* m_takeFromDisk = nullptr;
    bool m_requested = false;
    bool m_queued = false;
    bool m_editable = true;
    QString m_password;
    QHash<QString, QTreeWidgetItem*> m_nested;   ///< root hex → entry waiting for its contents
};

} // namespace eMule

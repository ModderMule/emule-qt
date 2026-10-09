#pragma once

/// @file ArchivePreviewPanel.h
/// @brief Archive content viewer panel — shows entries of ZIP/RAR/7z/ISO archives.
///
/// Embeddable QWidget for displaying archive contents in a tree view with
/// Name, Size, CRC, Attributes, Last Modified, Comment columns.
/// Supports automatic or manual scanning, and preview copy creation for
/// partial downloads (PartFiles).

#include <QDateTime>
#include <QList>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QStandardItemModel;
class QTreeView;

namespace eMule {

class ArchivePreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit ArchivePreviewPanel(QWidget* parent = nullptr);

    /// Set the file to display. Call before startScan() or setAutoScan().
    void setFile(const QString& filePath, uint64_t fileSize);

    /// For a download in progress: the byte ranges still missing (flat
    /// [start, end, ...] pairs, inclusive), and whether "Create preview file" is
    /// offered. Call after setFile(), which resets both.
    void setPartFile(const QList<qint64>& gapPairs, bool canCreatePreviewFile);

    /// If autoScan is true, calls startScan() automatically.
    void setAutoScan(bool autoScan);

    /// Manually trigger a (re-)scan of the archive contents.
    void startScan();

    /// Clear all entries and reset the panel.
    void clear();

signals:
    void scanFinished(int entryCount);
    /// "Create preview file": rebuild a readable archive from what has arrived
    /// (MFC CArchiveRecovery::recover, ArchivePreviewDlg.cpp:323-332).
    void previewFileRequested();

public:
    /// What a scan found, ready for the list. Public for the tests.
    struct Row {
        QString name;
        bool directory = false;
        quint64 size = 0;
        QString crc;
        QString attributes;
        QDateTime modified;
        QString comment;
        bool complete = true;
    };
    struct ScanOutput {
        QList<Row> rows;
        QString typeName;     ///< "ZIP", "RAR", libarchive's name; empty when unknown
        QString status;
        QString info;         ///< "Password protection,Solid,..."
        int fileCount = 0;    ///< without directories
        bool hasCrcAndComment = false;   ///< ZIP / RAR; other formats hide the two columns
    };
    /// Read @p filePath. Runs off the GUI thread.
    [[nodiscard]] static ScanOutput scanFile(const QString& filePath, const QList<qint64>& gapPairs);

private:
    void applyScan(const ScanOutput& output);
    void buildUi();

    QLabel*             m_archiveTypeLabel = nullptr;
    QLabel*             m_statusLabel      = nullptr;
    QPushButton*        m_updateBtn        = nullptr;
    QPushButton*        m_previewCopyBtn   = nullptr;
    QTreeView*          m_treeView         = nullptr;
    QStandardItemModel* m_model            = nullptr;
    QProgressBar*       m_progressBar      = nullptr;
    QLabel*             m_fileCountLabel   = nullptr;
    QLabel*             m_infoLabel        = nullptr;

    QList<qint64> m_gapPairs;

    QString  m_filePath;
    uint64_t m_fileSize = 0;
    bool     m_scanning = false;
};

} // namespace eMule

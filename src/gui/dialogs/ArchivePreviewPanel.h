#pragma once

/// @file ArchivePreviewPanel.h
/// @brief Archive content viewer panel — shows entries of ZIP/RAR/7z/ISO archives.
///
/// Embeddable QWidget for displaying archive contents in a tree view with
/// Name, Size, CRC, Attributes, Last Modified, Comment columns.
/// Supports automatic or manual scanning, and preview copy creation for
/// partial downloads (PartFiles).

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

    /// If autoScan is true, calls startScan() automatically.
    void setAutoScan(bool autoScan);

    /// Manually trigger a (re-)scan of the archive contents.
    void startScan();

    /// Clear all entries and reset the panel.
    void clear();

signals:
    void scanFinished(int entryCount);

private slots:
    void onScanComplete(int count);

private:
    void buildUi();

    QLabel*             m_archiveTypeLabel = nullptr;
    QLabel*             m_statusLabel      = nullptr;
    QPushButton*        m_updateBtn        = nullptr;
    QPushButton*        m_previewCopyBtn   = nullptr;
    QTreeView*          m_treeView         = nullptr;
    QStandardItemModel* m_model            = nullptr;
    QProgressBar*       m_progressBar      = nullptr;
    QLabel*             m_fileCountLabel   = nullptr;

    QString  m_filePath;
    uint64_t m_fileSize = 0;
    bool     m_scanning = false;
};

} // namespace eMule

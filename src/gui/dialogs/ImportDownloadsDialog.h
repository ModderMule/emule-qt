#pragma once

/// @file ImportDownloadsDialog.h
/// @brief Dialog for importing legacy download files (eMule, eDonkey, Overnet).
///
/// Matches the MFC CPartFileConvertDlg layout: current job progress,
/// a job queue list, and Add/Retry/Remove/Close buttons.

#include <QDialog>

class QLabel;
class QProgressBar;
class QTreeWidget;
class QPushButton;
class QTimer;
class QCborArray;

namespace eMule {

class IpcClient;

class ImportDownloadsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImportDownloadsDialog(IpcClient* ipc, QWidget* parent = nullptr);
    ~ImportDownloadsDialog() override;

private slots:
    void onAddImports();
    void onRetrySelected();
    void onRemoveSelected();
    void onRefresh();

private:
    void requestJobs();
    void updateJobList(const QCborArray& jobs);
    static QString statusString(int state);
    static QString formatSize(int64_t bytes);

    IpcClient* m_ipc = nullptr;
    QTimer* m_refreshTimer = nullptr;

    // Current job group
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;

    // Job queue
    QTreeWidget* m_jobList = nullptr;

    // Buttons
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_retryBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
};

} // namespace eMule

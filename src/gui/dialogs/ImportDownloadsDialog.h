#pragma once

/// @file ImportDownloadsDialog.h
/// @brief Dialog for importing legacy download files (eMule, eDonkey, Overnet).
///
/// Matches the MFC CPartFileConvertDlg layout: current job progress,
/// a job queue list, and Add/Retry/Remove/Close buttons.

#include "dialogs/RememberedSizeDialog.h"

class QLabel;
class QProgressBar;
class QTreeWidget;
class QPushButton;
class QTimer;
class QCborArray;

namespace eMule {

class IpcClient;

class ImportDownloadsDialog : public RememberedSizeDialog {
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
    void updateButtons();
    [[nodiscard]] QList<int> selectedJobIndices(bool (*accept)(int state)) const;
    static QString statusString(int state);
    // not Queued (1) / InProgress (2)
    static bool isRetryable(int state) { return state != 1 && state != 2; }
    static bool isRemovable(int state) { return state != 2; }

    static constexpr int kStateRole = Qt::UserRole + 1;
    static constexpr int kKeyRole = Qt::UserRole + 2;    // folder/filename

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

#pragma once

/// @file UsenetDetailsDialog.h
/// @brief Everything about one queued Usenet release: its files, their articles,
///        and where they came from.
///
/// The queue tree already shows a release's files as child rows, but only their
/// name, size and percentage — nothing that answers *why* a release is stuck. The
/// article tallies, the poster, the newsgroup and the NZB's own shortfall live
/// here, off GetUsenetItemDetails, which is polled only while this is on screen.
///
/// **Modeless, and never modal**, for the reason UsenetArchiveEntryDialog states
/// at length: a nested event loop unwound by a quit is the crash this module has
/// already paid for once. Being modeless means there is no loop to unwind.
///
/// Layout follows Newshosting's release dialog — a header grid of the facts that
/// apply to the whole release, over a table of its files.

#include <QCborMap>
#include <QDialog>
#include <QString>

class QLabel;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;

class UsenetDetailsDialog : public QDialog {
    Q_OBJECT

public:
    UsenetDetailsDialog(IpcClient* ipc, QString itemId, QString streamToken,
                        QWidget* parent = nullptr);

    /// Apply one GetUsenetItemDetails reply. Public because it is the whole of
    /// this dialog worth testing without a daemon behind it — the same reason
    /// UsenetArchiveEntryDialog::applyListing() is.
    void applyDetails(const QCborMap& details);

    enum Column {
        ColName = 0,
        ColSize,
        ColProgress,
        ColArticles,
        ColMissing,
        ColStatus,
        ColCount
    };

private slots:
    void poll();
    void onRowActivated(QTreeWidgetItem* item, int column);

private:
    void buildUi();
    void openFolder();

    IpcClient* m_ipc = nullptr;
    QString m_itemId;
    QString m_streamToken;

    QLabel* m_title = nullptr;
    QLabel* m_totalSize = nullptr;
    QLabel* m_date = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_health = nullptr;
    QLabel* m_poster = nullptr;
    QLabel* m_parts = nullptr;
    QLabel* m_groups = nullptr;
    QLabel* m_fileCount = nullptr;

    QTreeWidget* m_tree = nullptr;

    QTimer* m_poll = nullptr;
};

} // namespace eMule

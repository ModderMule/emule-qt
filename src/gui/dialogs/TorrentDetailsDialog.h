#pragma once

/// @file TorrentDetailsDialog.h
/// @brief The torrent inspector: Info, Activity and Options of one torrent, as
///        Transmission's inspector. Peers, Trackers and Files are lists in the
///        Torrents tab (TorrentListsPane), so they are not repeated here.
///
/// The row comes from the panel's TorrentListModel (pushed, ≤ 1 s old); the piece
/// availability and the options from GetTorrentDetails, polled while open. Modeless,
/// for the reason UsenetDetailsDialog gives: no nested event loop for a quit to unwind.

#include "dialogs/DetailDialog.h"

#include <QCborMap>
#include <QPointer>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;
class QTextBrowser;
class QTimer;
class QToolButton;

namespace eMule {

class IpcClient;
class TorrentListModel;
class TorrentPieceGrid;
struct TorrentRow;

class TorrentDetailsDialog : public DetailDialog {
    Q_OBJECT

public:
    /// Tab order, for tests and the panel.
    enum Tab { TabInfo = 0, TabActivity, TabOptions };

    TorrentDetailsDialog(IpcClient* ipc, const TorrentListModel* model, const QString& torrentId,
                         QWidget* parent = nullptr);

    /// The torrent shown; the panel reuses an open dialog.
    [[nodiscard]] QString torrentId() const { return m_id; }
    /// Show another torrent (the walker, or the panel reusing the dialog).
    void setTorrentId(const QString& id);

    /// Apply one GetTorrentDetails reply for the torrent shown. Public for tests.
    void setDetails(const QCborMap& details) override;
    /// Fill Info and Activity from a row. Public for tests.
    void setRow(const TorrentRow& row);

    [[nodiscard]] QTabWidget* tabs() const { return m_tabs; }
    [[nodiscard]] TorrentPieceGrid* pieceGrid() const { return m_grid; }
    /// The options the Options tab would send. Public for tests.
    [[nodiscard]] QCborMap optionsFromUi() const;

signals:
    /// SetTorrentOptions is about to go out. For tests.
    void optionsSent(const QCborMap& options);

private:
    QWidget* buildInfoTab();
    QWidget* buildActivityTab();
    QWidget* buildOptionsTab();
    void poll();
    void refreshRow();
    void applyOptions(const QCborMap& options);
    void onOptionsEdited();
    void sendOptions();
    void updateOptionEnables();
    void copyMagnet();

    IpcClient* m_ipc = nullptr;
    QPointer<const TorrentListModel> m_model;
    QString m_id;
    QString m_magnet;

    QLabel* m_icon = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_note = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTimer* m_pollTimer = nullptr;

    // Info
    QLabel* m_size = nullptr;
    QLabel* m_location = nullptr;
    QLabel* m_hashV1 = nullptr;
    QLabel* m_hashV2 = nullptr;
    QLabel* m_privacy = nullptr;
    QLabel* m_origin = nullptr;
    QLabel* m_magnetLabel = nullptr;
    QToolButton* m_copyMagnet = nullptr;
    QTextBrowser* m_comment = nullptr;

    // Activity
    QLabel* m_state = nullptr;
    QLabel* m_progress = nullptr;
    QLabel* m_have = nullptr;
    QLabel* m_availability = nullptr;
    QLabel* m_downloaded = nullptr;
    QLabel* m_uploaded = nullptr;
    QLabel* m_failed = nullptr;
    QLabel* m_ratio = nullptr;
    QPlainTextEdit* m_error = nullptr;
    TorrentPieceGrid* m_grid = nullptr;
    QToolButton* m_gridProgress = nullptr;
    QToolButton* m_gridAvailable = nullptr;
    QLabel* m_added = nullptr;
    QLabel* m_completed = nullptr;
    QLabel* m_lastActivity = nullptr;
    QLabel* m_downloadingTime = nullptr;
    QLabel* m_seedingTime = nullptr;
    QLabel* m_remaining = nullptr;

    // Options
    QCheckBox* m_limitDown = nullptr;
    QSpinBox* m_limitDownKb = nullptr;
    QCheckBox* m_limitUp = nullptr;
    QSpinBox* m_limitUpKb = nullptr;
    QComboBox* m_ratioMode = nullptr;
    QDoubleSpinBox* m_ratio100 = nullptr;
    QComboBox* m_seedTimeMode = nullptr;
    QSpinBox* m_seedMinutes = nullptr;
    QSpinBox* m_maxPeers = nullptr;
    QTimer* m_sendTimer = nullptr;
    /// the user changed an option that has not gone out yet
    bool m_optionsDirty = false;
    /// the Options tab is being filled from a reply
    bool m_fillingOptions = false;
    /// grid cells of a torrent that has every wanted piece; 0 = still downloading
    int m_seedBuckets = 0;
};

} // namespace eMule

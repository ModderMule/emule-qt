#pragma once

/// @file UsenetArchiveEntryDialog.h
/// @brief Pick which file inside a Usenet archive set to preview.
///
/// A release may hold several playable files — a season pack, or a feature
/// beside a trailer — and the preview URL addresses exactly one of them. This
/// asks which, and lists the rest of the archive greyed out so the answer to
/// "what else is in there?" is visible rather than implied.
///
/// **Modeless, and never modal.** A dialog opened from an IPC reply spins a
/// nested event loop, and a quit arriving during it unwinds every loop at once
/// while the socket-read notification that opened the box is still below on the
/// stack — see docs/protocol/http-cache-spec.md. Showing this one from the
/// user's own click already avoids that; being modeless means the feature
/// contains no nested event loop at all, so there is nothing left to unwind.
///
/// **Scanning costs articles, and none of them are wasted.** The header of the
/// second file inside a set sits past the first file's payload, so listing one
/// means fetching. Those articles are written to their real place in the
/// preallocated volume file, so a scan only brings forward work the download
/// was going to do anyway — cancelling costs nothing.

#include <QCborMap>
#include <QDeadlineTimer>
#include <QDialog>
#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTimer;

namespace eMule {

class IpcClient;

class UsenetArchiveEntryDialog : public QDialog {
    Q_OBJECT

public:
    UsenetArchiveEntryDialog(IpcClient* ipc, QString itemId, int fileIndex,
                             QWidget* parent = nullptr);

    /// The ordinal the user picked, or -1 for "whichever the daemon prefers".
    [[nodiscard]] int chosenEntry() const { return m_chosen; }

    /// Apply one ListUsenetArchiveEntries reply. Public because it is the whole
    /// of this dialog worth testing without a daemon behind it.
    void applyListing(const QCborMap& listing);

signals:
    /// The choice — emitted even when the dialog was never shown, which is the
    /// single-playable-file case.
    void entryChosen(int entry);

private slots:
    void poll();
    void revealIfStillScanning();

private:
    void buildUi();
    void updateButtons();
    void stopPolling();
    void finishWith(int entry);

    IpcClient* m_ipc = nullptr;
    QString m_itemId;
    int m_fileIndex = -1;

    QTreeWidget*  m_tree = nullptr;
    QLabel*       m_status = nullptr;
    QProgressBar* m_busy = nullptr;
    QPushButton*  m_previewBtn = nullptr;
    QPushButton*  m_keepScanningBtn = nullptr;

    QTimer* m_poll = nullptr;
    QDeadlineTimer m_deadline;

    bool m_scanning = true;
    bool m_revealed = false;
    bool m_done = false;
    int  m_chosen = -1;
};

} // namespace eMule

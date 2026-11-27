#pragma once

/// @file TorrentCreatorDialog.h
/// @brief Torrent Creator (#191), after qBittorrent's: a .torrent of a file, a folder, or
/// some files of one folder on the core's disk. The core hashes (IPC CreateTorrent);
/// the .torrent is saved on this machine and, when asked, seeded by the core.
///
/// Version 1, version 2 or hybrid: BitTorrent hashes only (SHA-1, SHA-256). With ed3k on,
/// a seeded torrent is hashed with BLAKE3 by its ed3k join and published as an ed3k set.

#include "dialogs/RememberedSizeDialog.h"

#include <QCborMap>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QMimeData;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace eMule {

class CheckComboBox;
class IpcClient;

class TorrentCreatorDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    /// @p path: a file or folder on the core's disk; @p onlyFiles: names below a folder
    /// @p path (several shared files of one folder), empty = all of it
    TorrentCreatorDialog(IpcClient* ipc, const QString& path, const QStringList& onlyFiles, QWidget* parent = nullptr);
    ~TorrentCreatorDialog() override;

    /// Opens a non-modal dialog that deletes itself.
    static void run(IpcClient* ipc, QWidget* parent, const QString& path, const QStringList& onlyFiles = {});
    /// A drop elsewhere (Torrents panel) the creator takes: a file or folder, or files of
    /// one folder, on this machine with a local core.
    [[nodiscard]] static bool acceptsDrop(IpcClient* ipc, const QMimeData* mime);
    /// Opens run() for such a drop; false = not one.
    static bool runFromDrop(IpcClient* ipc, QWidget* parent, const QMimeData* mime);

protected:
    void reject() override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onCreate();
    void onProgress(const QCborMap& progress);
    void onCreated(const QCborMap& result);

private:
    void setPath(const QString& path, const QStringList& onlyFiles);
    void selectFile();
    void selectFolder();
    void updateEd3kNote();
    void updatePublicTrackers();
    [[nodiscard]] bool takesDrops() const;
    void setBusy(bool busy);
    void loadSettings();
    void saveSettings() const;
    void saveTorrent(const QByteArray& data, const QString& name);
    [[nodiscard]] QCborMap params() const;
    [[nodiscard]] QString trackerText() const;
    void rememberCustomTrackers();
    void resetTrackerList();
    void storeTrackerList() const;

    IpcClient* m_ipc;
    QStringList m_onlyFiles;
    int m_jobId = 0;
    QString m_savePath;   // local .torrent target, asked before hashing

    QLineEdit* m_pathEdit;
    QLabel* m_selectionLabel;
    QPushButton* m_fileButton;
    QPushButton* m_folderButton;
    QComboBox* m_format;
    QComboBox* m_pieceSize;
    QCheckBox* m_private;
    QCheckBox* m_ignoreDotfiles;
    QCheckBox* m_startSeeding;
    QLabel* m_ed3kNote;
    CheckComboBox* m_publicTrackers;
    QPlainTextEdit* m_trackers;   // custom ones, below the public picks
    QPlainTextEdit* m_webSeeds;
    QPlainTextEdit* m_comment;
    QLineEdit* m_source;
    QProgressBar* m_progress;
    QDialogButtonBox* m_buttons;
    QPushButton* m_createButton;
};

} // namespace eMule

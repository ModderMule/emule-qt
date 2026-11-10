#pragma once

/// @file LegacyImportPage.h
/// @brief First start wizard page: take over settings and unfinished downloads from eMule or aMule.
///
/// The core looks for installs on its own host (IPC FindLegacyProfiles) and does the
/// import (ImportLegacyProfile). Both folders are prefilled and can be changed.
/// Unfinished downloads are moved, settings only read.
/// Settings make the core restart; the page waits for it to come back.

#include <QCborArray>
#include <QCborMap>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace eMule {

class IpcClient;

class LegacyImportPage : public QWidget {
    Q_OBJECT

public:
    explicit LegacyImportPage(IpcClient* ipc, QWidget* parent = nullptr);

    /// Carry out what is ticked; @p done(true, restarted) once it is safe to go on.
    /// Nothing ticked, or done before: at once. done(false, …) stays on the page.
    void apply(std::function<void(bool ok, bool restarted)> done);

    /// Waiting for the import or the core restart: no leaving the page.
    [[nodiscard]] bool busy() const { return m_busy; }

signals:
    void busyChanged(bool busy);
    /// The core's search is over (or there is no core to ask). Nothing found: the
    /// wizard leaves this page out.
    void searchFinished(bool found);

private:
    void requestProfiles();
    void showProfiles(const QCborArray& found);
    void onProfileChosen(int index);
    void onSettingsDirEdited();
    void browseSettings();
    void browseDownloads();
    void fillFromProfile(const QCborMap& profile);
    void updateEnabled();
    void setBusy(bool busy, const QString& status);
    [[nodiscard]] QStringList downloadDirs() const;
    void waitForCore(std::function<void(bool ok, bool restarted)> done);

    IpcClient* m_ipc = nullptr;
    QLabel* m_foundLabel = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QCheckBox* m_settingsCheck = nullptr;
    QLineEdit* m_settingsEdit = nullptr;
    QPushButton* m_settingsBrowse = nullptr;
    QCheckBox* m_partsCheck = nullptr;
    QLineEdit* m_partsEdit = nullptr;
    QPushButton* m_partsBrowse = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTimer* m_restartTimeout = nullptr;
    QCborArray m_found;
    QString m_probedDir;          ///< settings dir the download folders were filled from
    bool m_partsEdited = false;   ///< user typed download folders: not refilled
    bool m_busy = false;
    bool m_done = false;          ///< imported once: Back + Next does not repeat it
};

} // namespace eMule

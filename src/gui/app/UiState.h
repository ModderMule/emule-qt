#pragma once

/// @file UiState.h
/// @brief GUI layout state — persisted via Preferences to preferences.yml.
///
/// Keeps splitter positions and other window geometry in memory,
/// syncing to the uistate section of the shared preferences file.

#include <QByteArray>
#include <QHeaderView>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QSplitter>
#include <QString>

namespace eMule {

class UiState {
public:
    /// Load saved state from thePrefs.
    void load();

    /// Flush current state to thePrefs (call before thePrefs.save()).
    void save();

    /// Restore a splitter from saved sizes, connect it to auto-update.
    void bindServerSplitter(QSplitter* splitter);
    void bindKadSplitter(QSplitter* splitter);
    void bindTransferSplitter(QSplitter* splitter);
    void bindSharedHorzSplitter(QSplitter* splitter);
    void bindSharedVertSplitter(QSplitter* splitter);
    void bindMessagesSplitter(QSplitter* splitter);
    void bindIrcSplitter(QSplitter* splitter);
    void bindStatsSplitter(QSplitter* splitter);

    /// Restore a header view's column widths/sort order, connect it to auto-update.
    void bindHeaderView(QHeaderView* header, const QString& key);

    /// Restore the main window size. Call in the constructor before show().
    void bindMainWindow(QMainWindow* window);

    /// Capture current window geometry. Call from closeEvent().
    void captureMainWindow(QMainWindow* window);

    /// True if the window should be restored maximized.
    [[nodiscard]] bool isWindowMaximized() const { return m_windowMaximized; }

    /// Last selected options dialog page.
    [[nodiscard]] int optionsLastPage() const { return m_optionsLastPage; }
    void setOptionsLastPage(int page) { m_optionsLastPage = page; }

private:
    QList<int> m_serverSplitSizes;
    QList<int> m_kadSplitSizes;
    QList<int> m_transferSplitSizes;
    QList<int> m_sharedHorzSplitSizes;
    QList<int> m_sharedVertSplitSizes;
    QList<int> m_messagesSplitSizes;
    QList<int> m_ircSplitSizes;
    QList<int> m_statsSplitSizes;
    int  m_windowWidth     = 0;
    int  m_windowHeight    = 0;
    bool m_windowMaximized = false;
    int  m_optionsLastPage = 0;
    QMap<QString, QByteArray> m_headerStates;
};

/// Global UI state instance (GUI process only).
extern UiState theUiState;

} // namespace eMule

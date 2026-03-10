#pragma once

/// @file UiState.h
/// @brief GUI layout state — persisted to a separate uistate.yml file.
///
/// Keeps splitter positions, window geometry, header states and other
/// layout data in memory.  Reads/writes its own file so the daemon
/// cannot clobber GUI-only state when it saves preferences.yml.

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
    /// Load saved state from {configDir}/uistate.yml.
    void load(const QString& configDir);

    /// Write current state to {configDir}/uistate.yml.
    void save(const QString& configDir);

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

    /// Toolbar button order (empty = default).
    [[nodiscard]] const QList<int>& toolbarButtonOrder() const { return m_toolbarButtonOrder; }
    void setToolbarButtonOrder(const QList<int>& order) { m_toolbarButtonOrder = order; }

    /// Toolbar button style (Qt::ToolButtonStyle values 0-3).
    [[nodiscard]] int toolbarButtonStyle() const { return m_toolbarButtonStyle; }
    void setToolbarButtonStyle(int style) { m_toolbarButtonStyle = style; }

    /// Toolbar skin bitmap path (empty = default icons).
    [[nodiscard]] const QString& toolbarSkinPath() const { return m_toolbarSkinPath; }
    void setToolbarSkinPath(const QString& path) { m_toolbarSkinPath = path; }

    /// Skin profile INI path (empty = default, no skin).
    [[nodiscard]] const QString& skinProfilePath() const { return m_skinProfilePath; }
    void setSkinProfilePath(const QString& path) { m_skinProfilePath = path; }

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
    QList<int> m_toolbarButtonOrder;
    int  m_toolbarButtonStyle = 3;
    QString m_toolbarSkinPath;
    QString m_skinProfilePath;
    QMap<QString, QByteArray> m_headerStates;
};

/// Global UI state instance (GUI process only).
extern UiState theUiState;

} // namespace eMule

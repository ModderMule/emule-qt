#pragma once

/// @file CategoryTabBar.h
/// @brief The category tab strip, shared by the Transfers and Usenet tabs.
///
/// Owns the whole of the category *list* -- fetching it, mirroring it, editing
/// it and sending it back -- because that part is identical for both networks:
/// a category is one numbered row in `Preferences::categories()`, and its index
/// is the on-disk format for anything that points at one.
///
/// What is not identical is the bulk action a tab offers. ED2K has five
/// (`Ipc::CategoryAction`); Usenet has three, because `Stop` keeps a file and
/// drops its sources and `ResumeNext` ranks by the a4af priority, neither of
/// which Usenet has. So the widget builds the shared *tail* of the menu and
/// emits menuRequested() first, letting each panel put its own entries on top.
///
/// Ported behaviour, MFC references intact, from TransferPanel where all of this
/// lived until the Usenet queue became the second consumer.

#include "prefs/DownloadCategory.h"

#include <QList>
#include <QStringList>
#include <QTabBar>

class QMenu;

namespace eMule {

class IpcClient;

class CategoryTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit CategoryTabBar(QWidget* parent = nullptr);

    /// The IPC client to fetch and store the list through. Without one the bar
    /// still shows "All" and edits do nothing, which is what a disconnected GUI
    /// should look like.
    void setIpcClient(IpcClient* ipc) { m_ipc = ipc; }

    /// Needed only by "Open Incoming Folder" against a remote core.
    void setStreamToken(const QString& token) { m_streamToken = token; }

    /// Ask the daemon for the list and rebuild. Safe to call when disconnected.
    void requestCategories();

    /// The category the selected tab names, 0 for "All".
    [[nodiscard]] int currentCategory() const;

    /// Display name for a category index, for the tab bar and a Category column.
    [[nodiscard]] QString categoryTitle(int index) const;

    /// Every title, index-ordered — what a list model needs for its Category
    /// column.
    [[nodiscard]] QStringList categoryNames() const;

    [[nodiscard]] const QList<DownloadCategory>& categories() const { return m_categories; }

    /// The global incoming dir, as the daemon resolved index 0. The starting
    /// point for the category dialog's browse button.
    [[nodiscard]] QString defaultIncomingDir() const { return m_defaultIncomingDir; }

    /// Store an edited copy of the list, keeping every entry's old index.
    ///
    /// For an in-place change only — a colour, a priority. Adding, removing or
    /// reordering has to go through the widget's own entries, which maintain the
    /// old-index list that tells the daemon what moved where.
    void applyCategories(const QList<DownloadCategory>& categories);

    /// Open the context menu for @p tabIndex (-1 means empty tab-bar space,
    /// which still opens it — that is how "Add Category..." is reached before
    /// any category exists).
    void showCategoryMenu(int tabIndex, const QPoint& globalPos);

signals:
    /// The selected tab changed. Carries the *category* index, not the tab's.
    void currentCategoryChanged(int category);

    /// A fresh list arrived and the bar has been rebuilt.
    void categoriesReloaded();

    /// Emitted while the context menu is being built, before the shared tail.
    /// @p index is the category the menu belongs to; 0 is "All".
    void menuRequested(QMenu* menu, int index);

private:
    void rebuildTabs();
    void sendCategories(const QList<DownloadCategory>& categories, const QList<int>& oldIndex);
    void addCategoryInteractive();
    void editCategory(int index);
    void removeCategory(int index);

    IpcClient* m_ipc = nullptr;
    QString m_streamToken;

    /// The daemon's list, index-ordered — index 0 is "All". Mirrored here
    /// because it drives the tabs, the Category column and the assign menu at
    /// once, and refetching per use would be pure traffic. Refreshed on
    /// PushCategoriesChanged.
    QList<DownloadCategory> m_categories;

    /// Where each entry sat in the list the daemon last sent, so a reorder or a
    /// removal moves the downloads with their category instead of leaving them
    /// pointing at whatever now occupies the slot. -1 for a new entry.
    QList<int> m_categoryOldIndex;

    QString m_defaultIncomingDir;
};

} // namespace eMule

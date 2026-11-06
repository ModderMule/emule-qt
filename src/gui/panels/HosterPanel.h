#pragma once

/// @file HosterPanel.h
/// @brief The Hoster tab: downloads from links at file hosters (hoster.md).
///
/// A standalone tab like TorrentPanel; rows come from the core as CBOR (Ed3kHosterBridge,
/// 1100-1109 and the pushes 1150-1154). Image puzzles of the resolver are shown here in a
/// dialog of their own, whichever tab is open. No MFC original: laid out like the Torrents tab.
/// Items of a package (a search row, a named batch of links, a folder link) are children of
/// one package row; packages never nest. Link containers (DLC, RSDF, CCF) open here too:
/// by the Open DLC button, the context menu, a drop, the desktop or a container search row
/// (their bytes go to the core).

#include <QCborMap>
#include <QMap>
#include <QPointer>
#include <QWidget>

class QAction;
class QCborArray;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QInputDialog;
class QLabel;
class QMimeData;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class HosterPuzzleDialog;
class IpcClient;

namespace Ipc {
class IpcMessage;
}

class HosterPanel : public QWidget {
    Q_OBJECT

public:
    explicit HosterPanel(QWidget* parent = nullptr);
    ~HosterPanel() override;

    void setIpcClient(IpcClient* ipc);
    /// Adds hoster links without asking (a pasted or clicked link). @p package: see below.
    void addLinks(const QStringList& links, const QString& package = {});
    /// One download per entry of @p files: its first link, the others mirrors the core
    /// goes on with when that one fails (web search rows). @p package not empty: a new
    /// package of that name; @p packageId not 0: join that package instead.
    /// @p passwords: archive passwords for the unpack (a container's).
    void addLinks(const QList<QStringList>& files, const QString& package = {}, qint64 packageId = 0,
                  const QStringList& passwords = {});
    /// Asks for links, one per line; @p prefill lands in the box.
    void promptAddLinks(const QStringList& prefill = {});
    [[nodiscard]] QAction* addLinksAction() const { return m_addAction; }
    /// Sends each link container in @p paths to the core; a dialog asks which links to add.
    void openContainerFiles(const QStringList& paths);
    /// Sends one container's bytes (@p name: its file name, for the format) to the core.
    void openContainerData(const QString& name, const QByteArray& data);
    /// A file dialog for link containers, then openContainerFiles().
    void promptOpenContainer();
    /// Opens the container files of a drop (the main window's too). False: none in it.
    bool acceptContainerDrop(const QMimeData* mime);
    /// Asks for the password of item @p id's encrypted archive (one dialog per package, else
    /// per folder); the core keeps it with the package, tries it first and extracts again.
    /// Opens by itself when an extraction ends wanting one.
    void promptArchivePassword(qint64 id);
    /// Hoster downloads on (ed3kHosterEnabled), as the core last reported.
    [[nodiscard]] bool hostersEnabled() const { return m_enabled; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void refresh();
    void upsert(const QCborMap& item);
    void removeRow(qint64 id);
    void fillRow(QTreeWidgetItem* row, const QCborMap& item);
    /// The row of package @p id, made with @p name when missing.
    QTreeWidgetItem* packageRow(qint64 id, const QString& name);
    /// Sums the children of package @p id into its row; drops the row once empty.
    void updatePackage(qint64 id);
    void showContextMenu(const QPoint& pos);
    void act(const QString& op);
    void onPuzzle(const Ipc::IpcMessage& msg);
    void onLinks(const Ipc::IpcMessage& msg);
    /// An OpenHosterContainer result (PushHosterLinks with item 0): which links to add, or why none.
    void onContainer(const QCborMap& container, const QCborArray& links);
    void applyStatus(const QCborMap& ed3kStatus);
    [[nodiscard]] QList<qint64> selectedIds() const;
    /// http(s) links on the clipboard, one per line
    [[nodiscard]] static QStringList clipboardLinks();
    /// Opens a complete item's folder.
    void openRow(QTreeWidgetItem* row);
    void copyLinks();

    QPointer<IpcClient> m_ipc;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_statusLabel = nullptr;
    QAction* m_addAction = nullptr;
    QList<QPointer<HosterPuzzleDialog>> m_puzzleDialogs;   ///< open ones, for "Skip hoster"
    bool m_enabled = false;
    QMap<qint64, QTreeWidgetItem*> m_rows;
    QMap<qint64, QTreeWidgetItem*> m_packages;   ///< package id -> its row
    QMap<qint64, QCborMap> m_items;
    QMap<QString, QPointer<QInputDialog>> m_passwordAsks;   ///< open password dialogs by group
};

} // namespace eMule

#pragma once

/// @file HosterPanel.h
/// @brief The Hoster tab: downloads from links at file hosters (hoster.md).
///
/// A standalone tab like TorrentPanel; rows come from the core as CBOR (Ed3kHosterBridge,
/// 1100-1109 and the pushes 1150-1154). Image puzzles of the resolver are shown here in a
/// dialog of their own, whichever tab is open. No MFC original: laid out like the Torrents tab.
/// Items of a package (a search row, a named batch of links, a folder link) are children of
/// one package row; packages never nest.

#include <QCborMap>
#include <QMap>
#include <QPointer>
#include <QWidget>

class QAction;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

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
    void addLinks(const QList<QStringList>& files, const QString& package = {}, qint64 packageId = 0);
    /// Asks for links, one per line; @p prefill lands in the box.
    void promptAddLinks(const QStringList& prefill = {});
    [[nodiscard]] QAction* addLinksAction() const { return m_addAction; }
    /// Hoster downloads on (ed3kHosterEnabled), as the core last reported.
    [[nodiscard]] bool hostersEnabled() const { return m_enabled; }

signals:
    void optionsRequested();

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
    bool m_enabled = false;
    QMap<qint64, QTreeWidgetItem*> m_rows;
    QMap<qint64, QTreeWidgetItem*> m_packages;   ///< package id -> its row
    QMap<qint64, QCborMap> m_items;
};

} // namespace eMule

#pragma once

/// @file HosterPanel.h
/// @brief The Hoster tab: downloads from links at file hosters (hoster.md).
///
/// A standalone tab like TorrentPanel; rows come from the core as CBOR (Ed3kHosterBridge,
/// 1100-1109 and the pushes 1150-1154). Image puzzles of the resolver are shown here in a
/// dialog of their own, whichever tab is open. No MFC original: laid out like the Torrents tab.

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
    /// Adds hoster links without asking (a pasted or clicked link).
    void addLinks(const QStringList& links);
    /// One download per entry of @p files: its first link, the others mirrors the core
    /// goes on with when that one fails (web search rows).
    void addLinks(const QList<QStringList>& files);
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
    void showContextMenu(const QPoint& pos);
    void act(const QString& op);
    void onPuzzle(const Ipc::IpcMessage& msg);
    void onLinks(const Ipc::IpcMessage& msg);
    void applyStatus(const QCborMap& ed3kStatus);
    [[nodiscard]] QList<qint64> selectedIds() const;
    /// http(s) links on the clipboard, one per line
    [[nodiscard]] static QStringList clipboardLinks();

    QPointer<IpcClient> m_ipc;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_statusLabel = nullptr;
    QAction* m_addAction = nullptr;
    bool m_enabled = false;
    QMap<qint64, QTreeWidgetItem*> m_rows;
    QMap<qint64, QCborMap> m_items;
};

} // namespace eMule

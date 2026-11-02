#pragma once

/// @file Ed3kOwnSetsView.h
/// @brief "My ed3k Sets" in the Shared Files window (batch 15, IPC 1145-1148): the user's own
/// sets as rows (title, files, size, version, kind, sale) in place of the file list while the
/// tree node is selected. Row menu: Publish New Version, Copy Link (a hidden set's link is a
/// key: fetched with GetEd3kOwnSetLink, never logged), Rename, Sell… / Change Terms…, Stop
/// Selling / Resume Selling (Ed3kSellControl 4/5), Unpublish (asks).

#include <QCborArray>
#include <QWidget>

class QMenu;
class QTreeWidget;

namespace eMule {

namespace Ipc {
class IpcMessage;
}
class IpcClient;

class Ed3kOwnSetsView : public QWidget {
    Q_OBJECT

public:
    explicit Ed3kOwnSetsView(QWidget* parent = nullptr);

    void setIpcClient(IpcClient* ipc);
    /// GetEd3kOwnSets again.
    void refresh();
    /// The rows last fetched (GetEd3kOwnSets sets).
    [[nodiscard]] const QCborArray& rows() const { return m_rows; }
    /// Shows @p sets (GetEd3kOwnSets / PushEd3kOwnSetsChanged rows).
    void showRows(const QCborArray& sets);

    /// "Publish New Version…" of the Collection menu: the user picks one of the sets.
    void choosePublishNewVersion(QWidget* parent);
    /// Adds the actions of set @p id to @p menu (the row menu).
    void addSetActions(QMenu* menu, const QString& id);

private:
    void onContextMenu(const QPoint& pos);
    [[nodiscard]] QCborMap rowOf(const QString& id) const;
    void control(int action, const QString& id, const QString& title = {});
    void copyLink(const QString& id);
    void rename(const QString& id);
    void sell(const QString& id);
    void stopOrResume(const QString& id);
    void unpublish(const QString& id);
    void showError(const QString& what, const Ipc::IpcMessage& reply);

    IpcClient* m_ipc = nullptr;
    QTreeWidget* m_list = nullptr;
    QCborArray m_rows;
};

} // namespace eMule

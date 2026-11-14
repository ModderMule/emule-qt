#pragma once

/// @file FolderTreeModel.h
/// @brief The core's folder tree, loaded as it is expanded (FolderLister). Optionally
/// with share check boxes like the Options tree (bold above a checked folder,
/// MFC DirectoryTreeCtrl.cpp:248). Serves pickers and share trees of a remote core.

#include <QAbstractItemModel>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace eMule {

class IpcClient;

class FolderTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    static constexpr int PathRole = Qt::UserRole + 1;

    explicit FolderTreeModel(IpcClient* ipc, bool checkable, QObject* parent = nullptr);
    ~FolderTreeModel() override;

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex& parent = {}) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

    [[nodiscard]] QString filePath(const QModelIndex& index) const;
    /// The core's home folder, once the drives are in
    [[nodiscard]] QString homePath() const { return m_home; }

    void setCheckedPaths(const QStringList& paths);
    [[nodiscard]] QStringList checkedPaths() const;
    /// Ctrl+click: @p path and every folder below follow @p checked (MFC ShareSubDirTree)
    void setSubtreeChecked(const QString& path, bool checked);

    /// Load the folders down to @p path; revealed() names it once it is in the tree
    void reveal(const QString& path);

signals:
    void revealed(const QModelIndex& index);
    /// The drives are in; @p home is the core's home folder
    void homeKnown(const QString& home);
    /// Checks changed without a dataChanged (setSubtreeChecked's answer)
    void checksChanged();

private:
    struct Node;

    [[nodiscard]] Node* nodeOf(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexOf(const Node* node) const;
    void load(Node* node);
    void stepReveal();
    [[nodiscard]] bool hasCheckedBelow(const QString& path) const;

    IpcClient* m_ipc;
    bool m_checkable;
    std::unique_ptr<Node> m_root;
    QSet<QString> m_checked;
    QString m_home;
    QString m_revealKey;
};

} // namespace eMule

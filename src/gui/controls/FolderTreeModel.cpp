#include "pch.h"
/// @file FolderTreeModel.cpp
/// @brief The core's folder tree, loaded on expand — implementation.

#include "controls/FolderTreeModel.h"

#include "utils/FolderLister.h"
#include "utils/SharedDirState.h"

#include <QApplication>
#include <QFont>
#include <QStyle>

namespace eMule {

struct FolderTreeModel::Node {
    QString label;
    QString path;
    bool hasChildren = true;
    bool fetched = false;
    bool fetching = false;
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
};

FolderTreeModel::FolderTreeModel(IpcClient* ipc, bool checkable, QObject* parent)
    : QAbstractItemModel(parent)
    , m_ipc(ipc)
    , m_checkable(checkable)
    , m_root(std::make_unique<Node>())
{
    load(m_root.get());
}

FolderTreeModel::~FolderTreeModel() = default;

QModelIndex FolderTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    const Node* p = nodeOf(parent);
    if (column != 0 || row < 0 || row >= int(p->children.size()))
        return {};
    return createIndex(row, 0, p->children[size_t(row)].get());
}

QModelIndex FolderTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};
    return indexOf(nodeOf(child)->parent);
}

int FolderTreeModel::rowCount(const QModelIndex& parent) const
{
    return parent.column() > 0 ? 0 : int(nodeOf(parent)->children.size());
}

int FolderTreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

bool FolderTreeModel::hasChildren(const QModelIndex& parent) const
{
    const Node* n = nodeOf(parent);
    return n->fetched ? !n->children.empty() : n->hasChildren;
}

bool FolderTreeModel::canFetchMore(const QModelIndex& parent) const
{
    const Node* n = nodeOf(parent);
    return n->hasChildren && !n->fetched && !n->fetching;
}

void FolderTreeModel::fetchMore(const QModelIndex& parent)
{
    load(nodeOf(parent));
}

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const Node* n = nodeOf(index);
    switch (role) {
    case Qt::DisplayRole:
        return n->label;
    case Qt::ToolTipRole:
        return QDir::toNativeSeparators(n->path);
    case PathRole:
        return n->path;
    case Qt::DecorationRole:
        return QApplication::style()->standardIcon(n->parent == m_root.get() ? QStyle::SP_DriveHDIcon
                                                                               : QStyle::SP_DirIcon);
    case Qt::CheckStateRole:
        if (m_checkable)
            return m_checked.contains(n->path) ? Qt::Checked : Qt::Unchecked;
        break;
    case Qt::FontRole:
        if (m_checkable && hasCheckedBelow(n->path)) {
            QFont font;
            font.setBold(true);
            return font;
        }
        break;
    default:
        break;
    }
    return {};
}

bool FolderTreeModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!m_checkable || role != Qt::CheckStateRole || !index.isValid())
        return false;
    const QString path = nodeOf(index)->path;
    if (value.toInt() == Qt::Checked)
        m_checked.insert(path);
    else
        m_checked.remove(path);
    emit dataChanged(index, index, {Qt::CheckStateRole});
    // the folders above gain or lose their bold (MFC UpdateParentItems)
    for (QModelIndex up = index.parent(); up.isValid(); up = up.parent())
        emit dataChanged(up, up, {Qt::FontRole});
    return true;
}

Qt::ItemFlags FolderTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (m_checkable)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

QString FolderTreeModel::filePath(const QModelIndex& index) const
{
    return index.isValid() ? nodeOf(index)->path : QString();
}

void FolderTreeModel::setCheckedPaths(const QStringList& paths)
{
    // the tree stays as it is; views re-read every check and keep what is expanded
    emit layoutAboutToBeChanged();
    m_checked = QSet<QString>(paths.begin(), paths.end());
    emit layoutChanged();
}

QStringList FolderTreeModel::checkedPaths() const
{
    return QStringList(m_checked.begin(), m_checked.end());
}

void FolderTreeModel::setSubtreeChecked(const QString& path, bool checked)
{
    if (!checked) {
        setCheckedPaths(SharedDirState::withoutDir(checkedPaths(), path, true));
        emit checksChanged();
        return;
    }
    FolderLister::tree(m_ipc, path, this, [this](const QStringList& tree) {
        QStringList dirs = checkedPaths();
        QSet<QString> listed;
        for (const QString& d : std::as_const(dirs))
            listed.insert(SharedDirState::dirKey(d));
        for (const QString& d : tree)
            if (!listed.contains(SharedDirState::dirKey(d)))
                dirs.append(d);
        setCheckedPaths(dirs);
        emit checksChanged();
    });
}

void FolderTreeModel::reveal(const QString& path)
{
    m_revealKey = SharedDirState::dirKey(QDir::fromNativeSeparators(path));
    stepReveal();
}

// ---------------------------------------------------------------------------

FolderTreeModel::Node* FolderTreeModel::nodeOf(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : m_root.get();
}

QModelIndex FolderTreeModel::indexOf(const Node* node) const
{
    if (!node || node == m_root.get() || !node->parent)
        return {};
    const auto& siblings = node->parent->children;
    for (size_t i = 0; i < siblings.size(); ++i)
        if (siblings[i].get() == node)
            return createIndex(int(i), 0, const_cast<Node*>(node));
    return {};
}

void FolderTreeModel::load(Node* node)
{
    if (node->fetched || node->fetching)
        return;
    node->fetching = true;
    // nodes live as long as the model: a reset never drops them
    FolderLister::list(m_ipc, node->path, this, [this, node](bool, const QList<FolderEntry>& rows, const QString& home) {
        node->fetching = false;
        node->fetched = true;
        if (node == m_root.get() && !home.isEmpty()) {
            m_home = home;
            emit homeKnown(home);
        }
        if (!rows.isEmpty()) {
            beginInsertRows(indexOf(node), 0, int(rows.size()) - 1);
            for (const FolderEntry& e : rows) {
                auto child = std::make_unique<Node>();
                child->label = e.label;
                child->path = e.path;
                child->hasChildren = e.hasSubfolders;
                child->parent = node;
                node->children.push_back(std::move(child));
            }
            endInsertRows();
        } else if (node != m_root.get()) {
            const QModelIndex idx = indexOf(node);
            emit dataChanged(idx, idx);   // the arrow goes
        }
        stepReveal();
    });
}

void FolderTreeModel::stepReveal()
{
    if (m_revealKey.isEmpty())
        return;
    // deepest loaded folder at or above the target
    Node* best = m_root.get();
    for (Node* next = best; next;) {
        best = next;
        next = nullptr;
        qsizetype longest = -1;   // "/Volumes/X" before "/" on macOS
        for (const auto& child : best->children) {
            const QString key = SharedDirState::dirKey(child->path);
            if (key == m_revealKey) {
                m_revealKey.clear();
                emit revealed(indexOf(child.get()));
                return;
            }
            if (SharedDirState::isBelowKey(m_revealKey, key) && key.size() > longest) {
                next = child.get();
                longest = key.size();
            }
        }
    }
    if (best->fetched) {
        // not there: show the nearest folder above it
        m_revealKey.clear();
        if (best != m_root.get())
            emit revealed(indexOf(best));
        return;
    }
    load(best);
}

bool FolderTreeModel::hasCheckedBelow(const QString& path) const
{
    const QString key = SharedDirState::dirKey(path);
    return std::ranges::any_of(m_checked, [&key](const QString& checked) {
        return SharedDirState::isBelowKey(SharedDirState::dirKey(checked), key);
    });
}

} // namespace eMule

#include "pch.h"
/// @file UiState.cpp
/// @brief GUI layout state — reads/writes its own uistate.yml file.

#include "app/UiState.h"

#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QSaveFile>

#include <yaml-cpp/yaml.h>

#include "utils/Log.h"

namespace eMule {

UiState theUiState;

// ---------------------------------------------------------------------------
// YAML helpers
// ---------------------------------------------------------------------------

static QList<int> readIntList(const YAML::Node& node, const char* key)
{
    QList<int> result;
    if (auto n = node[key]; n && n.IsSequence()) {
        for (const auto& item : n)
            result.append(item.as<int>(0));
    }
    return result;
}

static void writeIntList(YAML::Emitter& out, const char* key, const QList<int>& list)
{
    out << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int v : list)
        out << v;
    out << YAML::EndSeq;
}

// ---------------------------------------------------------------------------
// load / save
// ---------------------------------------------------------------------------

void UiState::load(const QString& configDir)
{
    m_configDir = configDir;
    const QString path = configDir + QStringLiteral("/uistate.yml");

    try {
        YAML::Node root = YAML::LoadFile(path.toStdString());
        if (!root.IsMap())
            return;

        m_serverSplitSizes     = readIntList(root, "serverSplitSizes");
        m_kadSplitSizes        = readIntList(root, "kadSplitSizes");
        m_transferSplitSizes   = readIntList(root, "transferSplitSizes");
        m_sharedHorzSplitSizes = readIntList(root, "sharedHorzSplitSizes");
        m_sharedVertSplitSizes = readIntList(root, "sharedVertSplitSizes");
        m_messagesSplitSizes   = readIntList(root, "messagesSplitSizes");
        m_ircSplitSizes        = readIntList(root, "ircSplitSizes");
        m_statsSplitSizes      = readIntList(root, "statsSplitSizes");

        m_windowWidth      = root["windowWidth"].as<int>(m_windowWidth);
        m_windowHeight     = root["windowHeight"].as<int>(m_windowHeight);
        m_windowMaximized  = root["windowMaximized"].as<bool>(m_windowMaximized);
        m_optionsLastPage  = root["optionsLastPage"].as<int>(m_optionsLastPage);
        m_toolbarButtonStyle = root["toolbarButtonStyle"].as<int>(m_toolbarButtonStyle);

        m_toolbarSkinPath = QString::fromStdString(
            root["toolbarSkinPath"].as<std::string>(std::string{}));
        m_skinProfilePath = QString::fromStdString(
            root["skinProfilePath"].as<std::string>(std::string{}));

        m_toolbarButtonOrder = readIntList(root, "toolbarButtonOrder");

        if (auto hdr = root["headers"]; hdr && hdr.IsMap()) {
            for (const auto& pair : hdr) {
                auto key = QString::fromStdString(pair.first.as<std::string>());
                auto val = QByteArray::fromBase64(
                    QByteArray::fromStdString(pair.second.as<std::string>()));
                m_headerStates[key] = val;
            }
        }

        if (auto ste = root["statsTreeExpanded"]; ste && ste.IsSequence()) {
            for (const auto& item : ste)
                m_statsTreeExpanded.insert(QString::fromStdString(item.as<std::string>()));
        }
    } catch (const YAML::BadFile&) {
        // File doesn't exist yet — use defaults
    } catch (const YAML::Exception& ex) {
        logWarning(QStringLiteral("Failed to parse uistate.yml: %1 — using defaults")
                       .arg(QString::fromStdString(ex.what())));
    }
}

void UiState::save()
{
    if (!m_configDir.isEmpty())
        save(m_configDir);
}

void UiState::scheduleSave()
{
    if (m_configDir.isEmpty())
        return;   // load() hasn't run — nowhere to write yet

    if (!m_saveTimer) {
        m_saveTimer = std::make_unique<QTimer>();
        m_saveTimer->setSingleShot(true);
        m_saveTimer->setInterval(2000);
        QObject::connect(m_saveTimer.get(), &QTimer::timeout, m_saveTimer.get(),
                         [this] { save(); });
    }
    // start() on a running single-shot timer restarts it, so a burst of resize
    // events (dragging a column edge) collapses into a single write.
    m_saveTimer->start();
}

void UiState::save(const QString& configDir)
{
    const QString path = configDir + QStringLiteral("/uistate.yml");

    YAML::Emitter out;
    out << YAML::BeginMap;

    writeIntList(out, "serverSplitSizes",     m_serverSplitSizes);
    writeIntList(out, "kadSplitSizes",        m_kadSplitSizes);
    writeIntList(out, "transferSplitSizes",   m_transferSplitSizes);
    writeIntList(out, "sharedHorzSplitSizes", m_sharedHorzSplitSizes);
    writeIntList(out, "sharedVertSplitSizes", m_sharedVertSplitSizes);
    writeIntList(out, "messagesSplitSizes",   m_messagesSplitSizes);
    writeIntList(out, "ircSplitSizes",        m_ircSplitSizes);
    writeIntList(out, "statsSplitSizes",      m_statsSplitSizes);

    out << YAML::Key << "windowWidth"      << YAML::Value << m_windowWidth;
    out << YAML::Key << "windowHeight"     << YAML::Value << m_windowHeight;
    out << YAML::Key << "windowMaximized"  << YAML::Value << m_windowMaximized;
    out << YAML::Key << "optionsLastPage"  << YAML::Value << m_optionsLastPage;
    out << YAML::Key << "toolbarButtonStyle" << YAML::Value << m_toolbarButtonStyle;

    if (!m_toolbarSkinPath.isEmpty())
        out << YAML::Key << "toolbarSkinPath" << YAML::Value << m_toolbarSkinPath.toStdString();
    if (!m_skinProfilePath.isEmpty())
        out << YAML::Key << "skinProfilePath" << YAML::Value << m_skinProfilePath.toStdString();

    if (!m_toolbarButtonOrder.isEmpty())
        writeIntList(out, "toolbarButtonOrder", m_toolbarButtonOrder);

    if (!m_headerStates.isEmpty()) {
        out << YAML::Key << "headers" << YAML::Value << YAML::BeginMap;
        for (auto it = m_headerStates.cbegin(); it != m_headerStates.cend(); ++it)
            out << YAML::Key << it.key().toStdString()
                << YAML::Value << it.value().toBase64().toStdString();
        out << YAML::EndMap;
    }

    if (!m_statsTreeExpanded.isEmpty()) {
        out << YAML::Key << "statsTreeExpanded" << YAML::Value << YAML::BeginSeq;
        QList<QString> sorted(m_statsTreeExpanded.cbegin(), m_statsTreeExpanded.cend());
        sorted.sort();
        for (const auto& s : sorted)
            out << s.toStdString();
        out << YAML::EndSeq;
    }

    out << YAML::EndMap;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logError(QStringLiteral("Failed to open uistate.yml for writing: %1").arg(path));
        return;
    }

    file.write(out.c_str(), static_cast<qint64>(out.size()));
    file.write("\n", 1);

    if (!file.commit())
        logError(QStringLiteral("Failed to commit uistate.yml: %1").arg(path));
}

// ---------------------------------------------------------------------------
// Splitter bindings
// ---------------------------------------------------------------------------

void UiState::bindServerSplitter(QSplitter* splitter)
{
    bindSplitter(splitter, m_serverSplitSizes);
}

void UiState::bindKadSplitter(QSplitter* splitter)
{
    bindSplitter(splitter, m_kadSplitSizes);
}

void UiState::bindTransferSplitter(QSplitter* splitter)
{
    bindSplitter(splitter, m_transferSplitSizes);
}

void UiState::bindSharedHorzSplitter(QSplitter* splitter)
{
    bindSplitter(splitter, m_sharedHorzSplitSizes);
}

void UiState::bindSharedVertSplitter(QSplitter* splitter)
{
    bindSplitter(splitter, m_sharedVertSplitSizes);
}

void UiState::bindMessagesSplitter(QSplitter* splitter)
{
    bindSplitter(splitter, m_messagesSplitSizes);
}

void UiState::bindIrcSplitter(QSplitter* splitter)
{
    bindSplitter(splitter, m_ircSplitSizes);
}

void UiState::bindStatsSplitter(QSplitter* splitter)
{
    bindSplitter(splitter, m_statsSplitSizes);
}

// ---------------------------------------------------------------------------
// Stats tree expansion state
// ---------------------------------------------------------------------------

/// Compute a path key for a tree item (e.g. "Transfer/Uploads/Session").
static QString itemPath(QTreeWidgetItem* item)
{
    QStringList parts;
    for (auto* cur = item; cur; cur = cur->parent())
        parts.prepend(cur->text(0));
    return parts.join(QLatin1Char('/'));
}

/// Depth of a tree item (0 = top-level).
static int itemDepth(QTreeWidgetItem* item)
{
    int d = 0;
    for (auto* cur = item->parent(); cur; cur = cur->parent())
        ++d;
    return d;
}

void UiState::bindStatsTree(QTreeWidget* tree)
{
    // Apply defaults on first run
    if (m_statsTreeExpanded.isEmpty()) {
        m_statsTreeExpanded = {
            QStringLiteral("Transfer"),
            QStringLiteral("Connection"),
            QStringLiteral("Time Statistics")
        };
    }

    // Restore: walk top-level and first-level items
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto* top = tree->topLevelItem(i);
        const QString topPath = top->text(0);
        top->setExpanded(m_statsTreeExpanded.contains(topPath));

        for (int j = 0; j < top->childCount(); ++j) {
            auto* child = top->child(j);
            const QString childPath = topPath + QLatin1Char('/') + child->text(0);
            child->setExpanded(m_statsTreeExpanded.contains(childPath));
        }
    }

    // Auto-capture on expand/collapse (depth 0 and 1 only)
    QObject::connect(tree, &QTreeWidget::itemExpanded, tree, [this](QTreeWidgetItem* item) {
        if (itemDepth(item) <= 1) {
            m_statsTreeExpanded.insert(itemPath(item));
            scheduleSave();
        }
    });
    QObject::connect(tree, &QTreeWidget::itemCollapsed, tree, [this](QTreeWidgetItem* item) {
        if (itemDepth(item) <= 1) {
            m_statsTreeExpanded.remove(itemPath(item));
            scheduleSave();
        }
    });
}

// ---------------------------------------------------------------------------
// Window / header bindings
// ---------------------------------------------------------------------------

void UiState::bindMainWindow(QMainWindow* window)
{
    if (m_windowWidth > 0 && m_windowHeight > 0)
        window->resize(m_windowWidth, m_windowHeight);
}

void UiState::captureMainWindow(QMainWindow* window)
{
    m_windowMaximized = window->isMaximized();
    if (!m_windowMaximized) {
        m_windowWidth  = window->width();
        m_windowHeight = window->height();
    }
    // When maximized, keep the last saved normal size so it restores correctly.
}

void UiState::bindHeaderView(QHeaderView* header, const QString& key)
{
    // Restore saved state (overrides hardcoded defaults if available)
    if (auto it = m_headerStates.constFind(key); it != m_headerStates.constEnd() && !it->isEmpty())
        header->restoreState(*it);

    // Cache current state and auto-update on any change.
    // A header with no sections (model not attached yet, or just swapped to
    // nullptr) serialises to an empty state that restoreState() later rejects on
    // a column count mismatch — caching it would silently destroy the saved layout.
    auto capture = [this, header, key]() {
        if (header->count() == 0)
            return;
        m_headerStates[key] = header->saveState();
        scheduleSave();
    };
    capture();

    QObject::connect(header, &QHeaderView::sectionResized, header, capture);
    QObject::connect(header, &QHeaderView::sectionMoved,   header, capture);
    QObject::connect(header, &QHeaderView::sortIndicatorChanged, header, capture);

    // A header view's parent is its owning item view; guard its selection so a
    // model reset can't leave a stale index for a deferred header repaint to crash on.
    if (auto* view = qobject_cast<QAbstractItemView*>(header->parentWidget()))
        guardSelectionOnReset(view);
}

void UiState::applyHeaderState(QHeaderView* header, const QString& key)
{
    if (auto it = m_headerStates.constFind(key); it != m_headerStates.constEnd() && !it->isEmpty())
        header->restoreState(*it);
}

void UiState::guardSelectionOnReset(QAbstractItemView* view)
{
    if (!view)
        return;
    auto* model = view->model();
    if (!model)
        return;

    // Clear selection the moment a (source) reset begins, before the proxy's
    // persistent-index mapping is torn down — prevents a deferred header paint
    // from dereferencing a stale index in QSortFilterProxyModel::parent().
    // Resolve selectionModel() lazily (a later setModel() swaps it); bind the
    // connection lifetime to the view.
    QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset, view, [view] {
        if (auto* sel = view->selectionModel()) {
            sel->clearCurrentIndex();   // currentIndex also holds a persistent index
            sel->clearSelection();
        }
    });
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void UiState::bindSplitter(QSplitter* splitter, QList<int>& sizes)
{
    // Only restore a size list that matches the pane count — a mismatch means the
    // splitter was saved by a different layout, and QSplitter would silently
    // ignore the extra/missing entries.
    if (!sizes.isEmpty() && sizes.size() == splitter->count())
        splitter->setSizes(sizes);

    QObject::connect(splitter, &QSplitter::splitterMoved, splitter,
                     [this, splitter, &sizes]() {
        sizes = splitter->sizes();
        scheduleSave();
    });
}

} // namespace eMule

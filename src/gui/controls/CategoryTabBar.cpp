#include "pch.h"
/// @file CategoryTabBar.cpp
/// @brief The category tab strip — implementation.

#include "controls/CategoryTabBar.h"

#include "app/IpcClient.h"
#include "utils/StatusBarNotifier.h"
#include "dialogs/CategoryDialog.h"
#include "utils/PreviewLauncher.h"

#include <QCborArray>
#include <QCborMap>
#include <QColor>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>

namespace eMule {

using namespace Ipc;

CategoryTabBar::CategoryTabBar(QWidget* parent) : QTabBar(parent)
{
    setExpanding(false);
    setDocumentMode(true);
    addTab(tr("All"));
    setTabData(0, QVariant::fromValue(int64_t{0}));

    connect(this, &QTabBar::currentChanged, this, [this](int tabIdx) {
        emit currentCategoryChanged(
            tabIdx >= 0 ? int(tabData(tabIdx).toLongLong()) : 0);
    });

    // Right-click is the only way into the category editor, as in MFC. A click
    // on empty tab-bar space (tabAt returns -1) still opens the menu, because
    // that is how "Add Category..." is reached before any category exists.
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showCategoryMenu(tabAt(pos), mapToGlobal(pos));
    });

    // Double-click edits, matching srchybrid/TransferWnd.cpp:1253-1262.
    connect(this, &QTabBar::tabBarDoubleClicked, this, &CategoryTabBar::editCategory);

    // Drag to reorder, as MFC's tab control does (OnTabMovement,
    // srchybrid/TransferWnd.cpp:1264-1291). QTabBar moves the tab itself and
    // tells us afterwards; the daemon does the renumbering from the oldIndex
    // list, and its push rebuilds the bar either way.
    setMovable(true);
    connect(this, &QTabBar::tabMoved, this, [this](int from, int to) {
        // Index 0 is "All" and is pinned — MFC refuses the same move.
        if (from == 0 || to == 0 || from >= m_categories.size() || to >= m_categories.size()) {
            rebuildTabs();   // put the bar back
            return;
        }

        auto categories = m_categories;
        auto oldIndex = m_categoryOldIndex;
        categories.move(from, to);
        oldIndex.move(from, to);
        sendCategories(categories, oldIndex);
    });
}

int CategoryTabBar::currentCategory() const
{
    return currentIndex() >= 0 ? int(tabData(currentIndex()).toLongLong()) : 0;
}

QString CategoryTabBar::categoryTitle(int index) const
{
    if (index == 0)
        return tr("All");
    if (index > 0 && index < m_categories.size())
        return m_categories.at(index).displayName();

    // A download can outlive the category it names — a stale part.met, a stale
    // .nzbstate, or a list edited by another GUI. Showing the bare index says
    // more than an empty cell would.
    return tr("Cat %1").arg(index);
}

QStringList CategoryTabBar::categoryNames() const
{
    QStringList names;
    names.reserve(m_categories.size());
    for (int i = 0; i < m_categories.size(); ++i)
        names.append(categoryTitle(i));
    return names;
}

void CategoryTabBar::requestCategories()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetCategories), [this](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;

        m_categories.clear();
        m_categoryOldIndex.clear();
        for (const auto& value : resp.fieldArray(1)) {
            if (!value.isMap())
                continue;
            const QCborMap map = value.toMap();

            DownloadCategory cat;
            cat.title = map.value(QStringLiteral("title")).toString();
            cat.incomingPath = map.value(QStringLiteral("incoming")).toString();
            cat.comment = map.value(QStringLiteral("comment")).toString();
            cat.autocat = map.value(QStringLiteral("autocat")).toString();
            cat.autocatIsRegexp = map.value(QStringLiteral("autocatRegexp")).toBool(false);
            cat.regexp = map.value(QStringLiteral("regexp")).toString();
            cat.color = static_cast<quint32>(
                map.value(QStringLiteral("color")).toInteger(kCategoryColorAuto));
            cat.prio = static_cast<quint8>(map.value(QStringLiteral("prio")).toInteger(cat.prio));
            // Index 0's resolved path is the global incoming dir — the folder an
            // unset category falls back to, and the sensible starting point for
            // the browse button.
            if (m_categories.isEmpty())
                m_defaultIncomingDir = map.value(QStringLiteral("resolvedIncoming")).toString();

            m_categories.append(cat);
            m_categoryOldIndex.append(static_cast<int>(
                map.value(QStringLiteral("index")).toInteger(m_categories.size() - 1)));
        }

        // The daemon guarantees index 0; a reply without it would leave the tab
        // bar empty and the list behind it unreachable.
        if (m_categories.isEmpty()) {
            m_categories.append(DownloadCategory{.title = tr("All")});
            m_categoryOldIndex.append(0);
        }

        rebuildTabs();
        emit categoriesReloaded();
    });
}

void CategoryTabBar::applyCategories(const QList<DownloadCategory>& categories)
{
    if (categories.size() != m_categories.size())
        return;   // a structural change is not this function's job
    sendCategories(categories, m_categoryOldIndex);
}

void CategoryTabBar::showCategoryMenu(int tabIndex, const QPoint& globalPos)
{
    const int index = tabIndex >= 0 ? tabIndex : 0;
    const bool isAll = index == 0;

    QMenu menu(this);
    menu.addSection(isAll ? tr("Category") : tr("Category (%1)").arg(categoryTitle(index)));

    // Whatever this network can do to a whole category goes here, above the
    // entries that edit the category itself.
    emit menuRequested(&menu, index);

    if (!menu.isEmpty())
        menu.addSeparator();

    // MFC's MP_HM_OPENINC (srchybrid/TransferWnd.cpp:1021). The daemon may be on
    // another machine, so the shared helper decides between the file manager and
    // the browse page; "!N" is how that page addresses a category root.
    menu.addAction(tr("Open Incoming Folder"), this, [this, index] {
        const QString localPath = index > 0 && index < m_categories.size()
                                          && !m_categories.at(index).incomingPath.isEmpty()
                                      ? m_categories.at(index).incomingPath
                                      : m_defaultIncomingDir;
        const QString relPath = localPath == m_defaultIncomingDir
                                    ? QString{}
                                    : QStringLiteral("!%1").arg(index);
        openIncomingFolder(m_ipc, m_streamToken, localPath, relPath);
    });

    menu.addSeparator();
    menu.addAction(tr("Add Category..."), this, &CategoryTabBar::addCategoryInteractive);
    auto* editAct =
        menu.addAction(tr("Edit Category..."), this, [this, index] { editCategory(index); });
    auto* removeAct =
        menu.addAction(tr("Remove Category"), this, [this, index] { removeCategory(index); });
    // "All" is not a category the user owns — it has no folder, no colour and no
    // place in the list. MFC greys the same two entries on index 0
    // (srchybrid/TransferWnd.cpp:766-767).
    editAct->setEnabled(!isAll);
    removeAct->setEnabled(!isAll);

    menu.exec(globalPos);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void CategoryTabBar::rebuildTabs()
{
    // Remember the *category*, not the tab position: a removed category shifts
    // every tab behind it, and restoring by index would silently move the user
    // to a different one.
    const int64_t currentCat = currentIndex() >= 0 ? tabData(currentIndex()).toLongLong() : 0;
    int restoreIdx = 0;

    {
        // Scoped rather than function-wide, because the re-announcement below
        // has to escape it. Blocked so a half-built bar cannot momentarily
        // filter to a category that is about to move.
        const QSignalBlocker blocker(this);

        while (count() > 0)
            removeTab(0);

        // One tab per category, in list order — the order *is* the identity,
        // since that index is what part.met and .nzbstate store. Index 0 is the
        // "All" tab.
        for (int i = 0; i < m_categories.size(); ++i) {
            const int idx = addTab(categoryTitle(i));
            setTabData(idx, QVariant::fromValue(static_cast<int64_t>(i)));

            if (!m_categories.at(i).comment.isEmpty())
                setTabToolTip(idx, m_categories.at(i).comment);

            // MFC colours the tab text, not its background (SetTabTextColor,
            // srchybrid/TransferWnd.cpp:1035). kCategoryColorAuto means "leave
            // the theme alone", which is not the same as black.
            if (m_categories.at(i).color != kCategoryColorAuto)
                setTabTextColor(idx, QColor::fromRgb(m_categories.at(i).color));
        }

        for (int i = 0; i < count(); ++i) {
            if (tabData(i).toLongLong() == currentCat) {
                restoreIdx = i;
                break;
            }
        }
        setCurrentIndex(restoreIdx);
    }

    // The blocker suppressed currentChanged, so whoever is filtering still holds
    // whatever the previous tab set. Re-announce the tab actually selected now,
    // and synchronously — the caller expects the filter to be in step by the
    // time this returns.
    emit currentCategoryChanged(int(tabData(restoreIdx).toLongLong()));
}

void CategoryTabBar::sendCategories(const QList<DownloadCategory>& categories,
                                    const QList<int>& oldIndex)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    QCborArray rows;
    for (int i = 0; i < categories.size(); ++i) {
        const auto& cat = categories.at(i);
        rows.append(QCborMap{
            // -1 for an entry the user just created: nothing points at it yet.
            {QStringLiteral("oldIndex"),      i < oldIndex.size() ? oldIndex.at(i) : -1},
            {QStringLiteral("title"),         cat.title},
            {QStringLiteral("incoming"),      cat.incomingPath},
            {QStringLiteral("comment"),       cat.comment},
            {QStringLiteral("autocat"),       cat.autocat},
            {QStringLiteral("autocatRegexp"), cat.autocatIsRegexp},
            {QStringLiteral("regexp"),        cat.regexp},
            {QStringLiteral("color"),         static_cast<qint64>(cat.color)},
            {QStringLiteral("prio"),          static_cast<int>(cat.prio)},
        });
    }

    IpcMessage msg(IpcMsgType::SetCategories);
    msg.append(rows);
    m_ipc->sendRequest(msg, [this](const IpcMessage& resp) {
        if (!resp.isValid())
            return;   // connection dropped: the refetch below could not run either
        if (!resp.fieldBool(0)) {
            StatusBarNotifier::post(
                tr("Could not save categories: %1").arg(resp.field(1).toString()));
        }
        // Refetch either way: on success to pick up what the daemon sanitised (a
        // folder it refused, say), on failure to get back in step with what is
        // actually stored.
        requestCategories();
    });
}

void CategoryTabBar::addCategoryInteractive()
{
    CategoryDialog dialog(DownloadCategory{}, m_defaultIncomingDir, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto categories = m_categories;
    auto oldIndex = m_categoryOldIndex;
    categories.append(dialog.category());
    oldIndex.append(-1); // brand new — no downloads point at it yet
    sendCategories(categories, oldIndex);
}

void CategoryTabBar::editCategory(int index)
{
    if (index <= 0 || index >= m_categories.size())
        return;

    CategoryDialog dialog(m_categories.at(index), m_defaultIncomingDir, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto categories = m_categories;
    categories[index] = dialog.category();
    sendCategories(categories, m_categoryOldIndex);
}

void CategoryTabBar::removeCategory(int index)
{
    if (index <= 0 || index >= m_categories.size())
        return;

    // Removing renumbers: the downloads in this category fall back to "All" and
    // everything behind it shifts down one. Worth saying out loud, because the
    // list is the only place that mapping is recorded.
    if (QMessageBox::question(this, tr("Remove Category"),
                              tr("Remove the category \"%1\"?\n\n"
                                 "Its downloads keep their files and move to All.")
                                  .arg(categoryTitle(index)))
        != QMessageBox::Yes)
    {
        return;
    }

    // A plain removal: the surviving entries carry their old indices with them,
    // so the daemon moves each download to wherever its category ended up and
    // sends the ones that named this category back to All.
    auto categories = m_categories;
    auto oldIndex = m_categoryOldIndex;
    categories.removeAt(index);
    oldIndex.removeAt(index);

    sendCategories(categories, oldIndex);
}

} // namespace eMule

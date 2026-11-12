/// @file LinkGrabberPanel.cpp
/// @brief The Link Grabber tab — implementation.

#include "pch.h"
#include "panels/LinkGrabberPanel.h"

#include "app/IpcClient.h"
#include "utils/Clipboard.h"
#include "utils/FileTypeIcons.h"
#include "utils/HosterLinkStatus.h"
#include "utils/ListActivation.h"
#include "utils/MenuUtils.h"
#include "utils/StringUtils.h"
#include "IpcMessage.h"

#include <QApplication>
#include <QCborArray>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>


namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

enum Column { ColName, ColSize, ColHoster, ColStatus, ColSource, ColCount };

/// ColName data: the link id (0 on a package row) and the package id of a package row
constexpr int kLinkIdRole = Qt::UserRole + 1;
constexpr int kPackageIdRole = Qt::UserRole + 2;

/// Opening more than this in the browser asks first.
constexpr int kOpenWithoutAsking = 5;

/// Sorts numbers by the value kept in UserRole, text otherwise.
class GrabberRow : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override
    {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant a = data(col, Qt::UserRole);
        const QVariant b = other.data(col, Qt::UserRole);
        if (a.isValid() && b.isValid())
            return a.toDouble() < b.toDouble();
        return text(col).localeAwareCompare(other.text(col)) < 0;
    }
};

QList<qint64> idsOf(const QCborArray& a)
{
    QList<qint64> out;
    for (const QCborValue& v : a)
        out << v.toInteger();
    return out;
}

QCborArray toCbor(const QList<qint64>& ids)
{
    QCborArray a;
    for (const qint64 id : ids)
        a.append(id);
    return a;
}

QStringList lines(const QString& text)
{
    QStringList out;
    for (const QString& l : text.split(QLatin1Char('\n')))
        if (const QString t = l.trimmed(); !t.isEmpty())
            out << t;
    return out;
}

} // namespace

LinkGrabberPanel::LinkGrabberPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* bar = new QHBoxLayout;
    auto* title = new QLabel(tr("Link Grabber"), this);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    bar->addWidget(title);
    m_statusLabel = new QLabel(this);
    bar->addWidget(m_statusLabel, 1);
    auto* addBtn = new QPushButton(menuIcon("ListAdd.ico"), tr("Add Links..."), this);
    addBtn->setToolTip(tr("Links to crawl into the list, one per line"));
    bar->addWidget(addBtn);
    m_startBtn = new QPushButton(menuIcon("Start.ico"), tr("Start All"), this);
    m_startBtn->setToolTip(tr("Every enabled link that is not offline starts downloading"));
    bar->addWidget(m_startBtn);
    m_clearBtn = new QPushButton(menuIcon("DeleteAll.ico"), tr("Clear"), this);
    m_clearBtn->setToolTip(tr("Removes every link and stops the crawling"));
    bar->addWidget(m_clearBtn);
    layout->addLayout(bar);

    m_tree = new ListTreeWidget(this);
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({tr("File Name"), tr("Size"), tr("Hoster"), tr("Status"), tr("Link")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(ColName, Qt::AscendingOrder);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree, 1);
    m_tree->bindColumns(QStringLiteral("linkGrabberList"), {320});
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(true);

    connect(addBtn, &QPushButton::clicked, this, [this] { promptAddLinks(); });
    connect(m_startBtn, &QPushButton::clicked, this, [this] { confirm(m_packageRows.keys(), {}); });
    connect(m_clearBtn, &QPushButton::clicked, this, [this] {
        if (m_links.isEmpty() && m_crawling == 0)
            return;
        if (QMessageBox::question(this, tr("Clear"), tr("Remove every link from the Link Grabber?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes)
            cleanup(QStringLiteral("all"));
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &LinkGrabberPanel::showContextMenu);
    connect(m_tree, &QTreeWidget::itemChanged, this, &LinkGrabberPanel::onItemChanged);

    ListKeyHandlers keys;
    keys.activate = [this](const QModelIndex&) { confirm(selectedPackageIds(), selectedLinkIds()); };
    keys.remove = [this] { removeSelection(); };
    keys.rename = [this] { renamePackage(); };
    keys.copy = [this] { copyLinks(); };
    keys.paste = [this] {
        if (const QString text = QApplication::clipboard()->text(); !text.trimmed().isEmpty())
            grab(text, tr("Clipboard"));
    };
    keys.find = true;
    keys.expandKeys = true;
    keys.escapeDeselects = true;
    bindListKeys(m_tree, std::move(keys));
    setCrawling(0);
}

LinkGrabberPanel::~LinkGrabberPanel() = default;

void LinkGrabberPanel::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    if (!m_ipc)
        return;
    connect(m_ipc, &IpcClient::connected, this, &LinkGrabberPanel::refresh);
    connect(m_ipc, &IpcClient::linkGrabberChanged, this, [this](const IpcMessage& msg) {
        const QCborMap m = msg.fieldMap(0);
        apply(m, m.value(QStringLiteral("all")).toBool());
    });
    if (m_ipc->isConnected())
        refresh();
}

void LinkGrabberPanel::promptAddLinks(const QStringList& prefill)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add links to the Link Grabber"));
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(tr("Links, or text with links in it:"), &dlg));
    // nothing handed in: the clipboard's http(s) links, if any
    const QStringList links = prefill.isEmpty() ? Clipboard::httpLinksIn(Clipboard::text()) : prefill;
    auto* edit = new QPlainTextEdit(links.join(QLatin1Char('\n')), &dlg);
    edit->selectAll();
    edit->setMinimumSize(520, 180);
    layout->addWidget(edit);
    auto* packageRow = new QHBoxLayout;
    packageRow->addWidget(new QLabel(tr("Package Name:"), &dlg));
    auto* packageEdit = new QLineEdit(&dlg);
    packageEdit->setPlaceholderText(tr("optional"));
    packageEdit->setToolTip(tr("All links found go into one package of this name"));
    packageRow->addWidget(packageEdit, 1);
    layout->addLayout(packageRow);
    layout->addWidget(new QLabel(tr("Archive passwords (one per line, optional):"), &dlg));
    auto* passwordEdit = new QPlainTextEdit(&dlg);
    passwordEdit->setMaximumHeight(60);
    layout->addWidget(passwordEdit);
    auto* deep = new QCheckBox(tr("Deep crawl: look for hoster links on the pages too"), &dlg);
    deep->setToolTip(tr("A link no plugin knows is loaded as a web page, and every hoster link on it is "
                        "added. Takes longer."));
    deep->setVisible(m_deep);
    layout->addWidget(deep);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    grab(edit->toPlainText(), tr("Add Links"), m_deep && deep->isChecked(), packageEdit->text(),
         lines(passwordEdit->toPlainText()));
}

void LinkGrabberPanel::grab(const QString& text, const QString& source, bool deep, const QString& package,
                            const QStringList& passwords, bool clipboard)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Link Grabber"), tr("Not connected to the eMule core."));
        return;
    }
    IpcMessage req(IpcMsgType::GrabLinks);
    req.append(text);
    req.append(source);
    req.append(deep);
    req.append(package.trimmed());
    req.append(QCborArray::fromStringList(passwords));
    req.append(false);   // into the list
    req.append(clipboard);
    QPointer<LinkGrabberPanel> self(this);
    m_ipc->sendRequest(std::move(req), [self, clipboard](const IpcMessage& r) {
        // the clipboard asks quietly: a copied text without new links is no error
        if (self && r.isValid() && !r.fieldBool(0) && !clipboard)
            QMessageBox::warning(self, tr("Link Grabber"), r.fieldMap(1).value(QStringLiteral("error")).toString());
    });
}

// ---- private ----

void LinkGrabberPanel::refresh()
{
    if (!m_ipc)
        return;
    QPointer<LinkGrabberPanel> self(this);
    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetLinkGrabber), [self](const IpcMessage& r) {
        if (self && r.isValid() && r.fieldBool(0))
            self->apply(r.fieldMap(1), true);
    });
}

void LinkGrabberPanel::apply(const QCborMap& m, bool all)
{
    m_filling = true;
    m_tree->setSortingEnabled(false);
    if (all) {
        m_tree->clear();
        m_packages.clear();
        m_links.clear();
        m_packageRows.clear();
        m_linkRows.clear();
        if (m.contains(QStringLiteral("deep")))
            m_deep = m.value(QStringLiteral("deep")).toBool();
    }
    QSet<qint64> touched;
    for (const QCborValue& v : m.value(QStringLiteral("packages")).toArray()) {
        upsertPackage(v.toMap());
        touched.insert(v.toMap().value(QStringLiteral("id")).toInteger());
    }
    for (const QCborValue& v : m.value(QStringLiteral("links")).toArray()) {
        const QCborMap k = v.toMap();
        // a link that moved: its old package's sums too
        if (const auto old = m_links.constFind(k.value(QStringLiteral("id")).toInteger()); old != m_links.cend())
            touched.insert(old->value(QStringLiteral("packageId")).toInteger());
        upsertLink(k);
        touched.insert(k.value(QStringLiteral("packageId")).toInteger());
    }
    for (const qint64 id : idsOf(m.value(QStringLiteral("removedLinks")).toArray())) {
        touched.insert(m_links.value(id).value(QStringLiteral("packageId")).toInteger());
        removeLink(id);
    }
    for (const qint64 id : idsOf(m.value(QStringLiteral("removedPackages")).toArray())) {
        touched.remove(id);
        removePackage(id);
    }
    for (const qint64 id : std::as_const(touched))
        updatePackage(id);
    m_tree->setSortingEnabled(true);
    m_filling = false;
    setCrawling(int(m.value(QStringLiteral("crawling")).toInteger()));
}

void LinkGrabberPanel::upsertPackage(const QCborMap& p)
{
    const qint64 id = p.value(QStringLiteral("id")).toInteger();
    if (id == 0)
        return;
    m_packages.insert(id, p);
    QTreeWidgetItem* row = m_packageRows.value(id);
    if (!row) {
        row = new GrabberRow(m_tree);
        row->setData(ColName, kPackageIdRole, id);
        row->setIcon(ColName, menuIcon("FolderOpen.ico"));
        row->setExpanded(true);
        m_packageRows.insert(id, row);
    }
    row->setToolTip(ColSource, p.value(QStringLiteral("source")).toString());
    row->setText(ColSource, p.value(QStringLiteral("source")).toString());
    updatePackage(id);
}

void LinkGrabberPanel::upsertLink(const QCborMap& k)
{
    const qint64 id = k.value(QStringLiteral("id")).toInteger();
    const qint64 pkg = k.value(QStringLiteral("packageId")).toInteger();
    QTreeWidgetItem* parent = m_packageRows.value(pkg);
    if (id == 0 || !parent)
        return;
    m_links.insert(id, k);
    QTreeWidgetItem* row = m_linkRows.value(id);
    if (row && row->parent() != parent) {
        // moved to another package
        if (QTreeWidgetItem* old = row->parent())
            old->removeChild(row);
        parent->addChild(row);
    }
    if (!row) {
        row = new GrabberRow(parent);
        row->setData(ColName, kLinkIdRole, id);
        m_linkRows.insert(id, row);
    }
    fillLink(row, k);
}

void LinkGrabberPanel::removeLink(qint64 id)
{
    m_links.remove(id);
    delete m_linkRows.take(id);
}

void LinkGrabberPanel::removePackage(qint64 id)
{
    m_packages.remove(id);
    QTreeWidgetItem* row = m_packageRows.take(id);
    if (!row)
        return;
    // its links went with it
    for (int i = row->childCount() - 1; i >= 0; --i) {
        const qint64 link = row->child(i)->data(ColName, kLinkIdRole).toLongLong();
        m_links.remove(link);
        m_linkRows.remove(link);
    }
    delete row;
}

void LinkGrabberPanel::updatePackage(qint64 id)
{
    QTreeWidgetItem* row = m_packageRows.value(id);
    if (!row)
        return;
    const QCborMap p = m_packages.value(id);
    qint64 size = 0;
    int online = 0, offline = 0, checking = 0;
    QStringList hosts;
    for (int i = 0; i < row->childCount(); ++i) {
        const QCborMap k = m_links.value(row->child(i)->data(ColName, kLinkIdRole).toLongLong());
        size += k.value(QStringLiteral("size")).toInteger();
        switch (int(k.value(QStringLiteral("status")).toInteger())) {
        case gui::LinkOnline: ++online; break;
        case gui::LinkOffline: ++offline; break;
        case gui::LinkChecking: ++checking; break;
        default: break;
        }
        if (const QString h = k.value(QStringLiteral("host")).toString(); !h.isEmpty() && !hosts.contains(h))
            hosts << h;
    }
    const bool was = m_filling;
    m_filling = true;
    row->setText(ColName, QStringLiteral("%1 (%2)").arg(p.value(QStringLiteral("name")).toString()).arg(row->childCount()));
    const QStringList pw = p.value(QStringLiteral("passwords")).toVariant().toStringList();
    row->setToolTip(ColName, pw.isEmpty() ? QString() : tr("Archive passwords: %1").arg(pw.join(QStringLiteral(", "))));
    row->setText(ColSize, size > 0 ? formatByteSize(size) : QString());
    row->setData(ColSize, Qt::UserRole, size);
    row->setText(ColHoster, hosts.join(QStringLiteral(", ")));
    QString status = tr("%1 online").arg(online);
    if (offline > 0)
        status += QStringLiteral(", ") + tr("%1 offline").arg(offline);
    if (checking > 0)
        status += QStringLiteral(", ") + tr("%1 checking").arg(checking);
    row->setText(ColStatus, status);
    m_filling = was;
}

void LinkGrabberPanel::fillLink(QTreeWidgetItem* row, const QCborMap& k)
{
    const bool was = m_filling;
    m_filling = true;
    const QString url = k.value(QStringLiteral("url")).toString();
    QString name = k.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = QUrl(url).fileName().isEmpty() ? url : QUrl(url).fileName();
    row->setText(ColName, name);
    row->setIcon(ColName, fileTypeIconForName(name));
    row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
    const bool enabled = k.value(QStringLiteral("enabled")).toBool();
    row->setCheckState(ColName, enabled ? Qt::Checked : Qt::Unchecked);
    const qint64 size = k.value(QStringLiteral("size")).toInteger();
    row->setText(ColSize, size > 0 ? formatByteSize(size) : QString());
    row->setData(ColSize, Qt::UserRole, size);
    row->setText(ColHoster, k.value(QStringLiteral("host")).toString());
    const int status = int(k.value(QStringLiteral("status")).toInteger());
    QString text;
    switch (status) {
    case gui::LinkChecking: text = tr("Checking..."); break;
    case gui::LinkOnline: text = tr("Online"); break;
    case gui::LinkOffline: text = tr("Offline"); break;
    default: text = tr("Unknown"); break;
    }
    if (k.value(QStringLiteral("dupe")).toBool())
        text = tr("Already in downloads");
    row->setText(ColStatus, text);
    row->setIcon(ColStatus, gui::linkStatusIcon(status));
    row->setText(ColSource, url);
    row->setToolTip(ColSource, url);
    // disabled, offline or a dupe: greyed, they are not started
    const bool grey = !enabled || status == gui::LinkOffline || k.value(QStringLiteral("dupe")).toBool();
    const QVariant fg = grey ? QVariant(palette().brush(QPalette::Disabled, QPalette::Text)) : QVariant();
    for (int c = 0; c < ColCount; ++c)
        row->setData(c, Qt::ForegroundRole, fg);
    m_filling = was;
}

void LinkGrabberPanel::setCrawling(int n)
{
    m_crawling = n;
    int online = 0;
    for (const QCborMap& k : std::as_const(m_links))
        online += k.value(QStringLiteral("status")).toInteger() == gui::LinkOnline ? 1 : 0;
    QString text = tr("%n link(s) in %1 package(s)", nullptr, int(m_links.size())).arg(m_packages.size());
    if (!m_links.isEmpty())
        text += QStringLiteral(", ") + tr("%1 online").arg(online);
    if (n > 0)
        text += QStringLiteral(" — ") + tr("crawling %n link(s)...", nullptr, n);
    m_statusLabel->setText(text);
    m_startBtn->setEnabled(!m_links.isEmpty());
    m_clearBtn->setEnabled(!m_links.isEmpty() || n > 0);
    emit countChanged(int(m_links.size()), n);
}

void LinkGrabberPanel::showContextMenu(const QPoint& pos)
{
    const QList<qint64> links = selectedLinkIds();
    const QList<qint64> pkgs = touchedPackageIds();
    QMenu menu(this);
    QAction* start = menu.addAction(menuIcon("Start.ico"), tr("Start Downloads"));
    menu.setDefaultAction(start);
    menu.addSeparator();
    QAction* rename = menu.addAction(menuIcon("Rename.ico"), tr("Rename Package..."));
    QAction* password = menu.addAction(menuIcon("Security.ico"), tr("Set Passwords..."));
    QAction* merge = menu.addAction(menuIcon("ListAdd.ico"), tr("Merge into New Package..."));
    QAction* split = menu.addAction(menuIcon("SplitWindow.ico"), tr("Split by Hoster"));
    menu.addSeparator();
    QAction* enable = menu.addAction(tr("Enable"));
    QAction* disable = menu.addAction(tr("Disable"));
    QAction* recheck = menu.addAction(menuIcon("Restore.ico"), tr("Check Again"));
    menu.addSeparator();
    QAction* remove = menu.addAction(menuIcon("ListRemove.ico"), tr("Remove"));
    QMenu* cleanMenu = menu.addMenu(menuIcon("DeleteSelected.ico"), tr("Remove from List"));
    QAction* offline = cleanMenu->addAction(tr("Offline Links"));
    QAction* disabled = cleanMenu->addAction(tr("Disabled Links"));
    QAction* dupes = cleanMenu->addAction(tr("Links Already in Downloads"));
    menu.addSeparator();
    QAction* copy = menu.addAction(menuIcon("Copy.ico"), tr("Copy Links"));
    QAction* browser = menu.addAction(menuIcon("Web.ico"), tr("Open in Browser"));
    menu.addSeparator();
    QAction* add = menu.addAction(menuIcon("ListAdd.ico"), tr("Add Links..."));
    QAction* stop = nullptr;
    if (m_crawling > 0)
        stop = menu.addAction(menuIcon("Stop.ico"), tr("Stop Crawling"));
    for (QAction* a : {start, enable, disable, recheck, remove, copy, browser, merge})
        a->setEnabled(!links.isEmpty());
    rename->setEnabled(pkgs.size() == 1);
    password->setEnabled(!pkgs.isEmpty());
    split->setEnabled(!pkgs.isEmpty());

    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == start) {
        confirm(selectedPackageIds(), links);
    } else if (chosen == rename) {
        renamePackage();
    } else if (chosen == password) {
        setPasswords();
    } else if (chosen == merge) {
        mergeSelection();
    } else if (chosen == split) {
        IpcMessage req(IpcMsgType::MoveGrabberLinks);
        req.append(QCborArray{});
        req.append(qint64(0));
        req.append(QString());
        req.append(toCbor(pkgs));
        send(std::move(req));
    } else if (chosen == enable || chosen == disable) {
        IpcMessage req(IpcMsgType::SetGrabberLinksEnabled);
        req.append(toCbor(links));
        req.append(chosen == enable);
        send(std::move(req));
    } else if (chosen == recheck) {
        IpcMessage req(IpcMsgType::RecheckGrabberLinks);
        req.append(toCbor(links));
        req.append(QCborArray{});
        send(std::move(req));
    } else if (chosen == remove) {
        removeSelection();
    } else if (chosen == offline) {
        cleanup(QStringLiteral("offline"));
    } else if (chosen == disabled) {
        cleanup(QStringLiteral("disabled"));
    } else if (chosen == dupes) {
        cleanup(QStringLiteral("dupes"));
    } else if (chosen == copy) {
        copyLinks();
    } else if (chosen == browser) {
        openInBrowser();
    } else if (chosen == add) {
        promptAddLinks();
    } else if (chosen == stop) {
        send(IpcMessage(IpcMsgType::AbortGrabberCrawls));
    }
}

void LinkGrabberPanel::onItemChanged(QTreeWidgetItem* row, int column)
{
    if (m_filling || column != ColName)
        return;
    const qint64 id = row->data(ColName, kLinkIdRole).toLongLong();
    if (id == 0)
        return;
    const bool on = row->checkState(ColName) == Qt::Checked;
    if (on == m_links.value(id).value(QStringLiteral("enabled")).toBool())
        return;
    // the selection follows a click on a selected row (a whole block at once)
    QList<qint64> ids{id};
    if (row->isSelected())
        ids = selectedLinkIds();
    IpcMessage req(IpcMsgType::SetGrabberLinksEnabled);
    req.append(toCbor(ids));
    req.append(on);
    send(std::move(req));
}

QList<qint64> LinkGrabberPanel::selectedLinkIds() const
{
    QList<qint64> out;
    const auto take = [&out](const QTreeWidgetItem* row) {
        if (const qint64 id = row->data(ColName, kLinkIdRole).toLongLong(); id != 0 && !out.contains(id))
            out << id;
    };
    for (QTreeWidgetItem* row : m_tree->selectedItems()) {
        take(row);
        for (int i = 0; i < row->childCount(); ++i)
            take(row->child(i));
    }
    return out;
}

QList<qint64> LinkGrabberPanel::selectedPackageIds() const
{
    QList<qint64> out;
    for (QTreeWidgetItem* row : m_tree->selectedItems())
        if (const qint64 id = row->data(ColName, kPackageIdRole).toLongLong(); id != 0)
            out << id;
    return out;
}

QList<qint64> LinkGrabberPanel::touchedPackageIds() const
{
    QList<qint64> out;
    for (QTreeWidgetItem* row : m_tree->selectedItems()) {
        const QTreeWidgetItem* pkg = row->parent() ? row->parent() : row;
        if (const qint64 id = pkg->data(ColName, kPackageIdRole).toLongLong(); id != 0 && !out.contains(id))
            out << id;
    }
    return out;
}

void LinkGrabberPanel::confirm(const QList<qint64>& packageIds, const QList<qint64>& linkIds)
{
    if (!m_ipc || (packageIds.isEmpty() && linkIds.isEmpty()))
        return;
    IpcMessage req(IpcMsgType::ConfirmGrabberLinks);
    req.append(toCbor(packageIds));
    req.append(toCbor(linkIds));
    req.append(false);   // offline links stay
    QPointer<LinkGrabberPanel> self(this);
    m_ipc->sendRequest(std::move(req), [self](const IpcMessage& r) {
        if (!self || !r.isValid())
            return;
        if (!r.fieldBool(0))
            QMessageBox::warning(self, tr("Link Grabber"), r.fieldMap(1).value(QStringLiteral("error")).toString());
        else if (r.fieldMap(1).value(QStringLiteral("ids")).toArray().isEmpty())
            QMessageBox::information(self, tr("Link Grabber"),
                                     tr("Nothing to start: the links are disabled, offline or downloading already."));
    });
}

void LinkGrabberPanel::renamePackage()
{
    const QList<qint64> pkgs = touchedPackageIds();
    if (pkgs.size() != 1)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Package"), tr("Package name:"), QLineEdit::Normal,
                                               m_packages.value(pkgs.first()).value(QStringLiteral("name")).toString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    IpcMessage req(IpcMsgType::EditGrabberPackages);
    req.append(toCbor(pkgs));
    req.append(name);
    send(std::move(req));
}

void LinkGrabberPanel::setPasswords()
{
    const QList<qint64> pkgs = touchedPackageIds();
    if (pkgs.isEmpty())
        return;
    const QStringList now = m_packages.value(pkgs.first()).value(QStringLiteral("passwords")).toVariant().toStringList();
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, tr("Set Passwords"),
        tr("Archive passwords for %n package(s), one per line; extraction tries them:", nullptr, int(pkgs.size())),
        now.join(QLatin1Char('\n')), &ok);
    if (!ok)
        return;
    IpcMessage req(IpcMsgType::EditGrabberPackages);
    req.append(toCbor(pkgs));
    req.append(QString());
    req.append(QCborArray::fromStringList(lines(text)));
    send(std::move(req));
}

void LinkGrabberPanel::mergeSelection()
{
    const QList<qint64> links = selectedLinkIds();
    if (links.isEmpty())
        return;
    const QList<qint64> pkgs = touchedPackageIds();
    const QString suggest = pkgs.isEmpty() ? QString() : m_packages.value(pkgs.first()).value(QStringLiteral("name")).toString();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Merge into New Package"), tr("Package name:"), QLineEdit::Normal,
                                               suggest, &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    IpcMessage req(IpcMsgType::MoveGrabberLinks);
    req.append(toCbor(links));
    req.append(qint64(0));
    req.append(name);
    send(std::move(req));
}

void LinkGrabberPanel::removeSelection()
{
    const QList<qint64> links = selectedLinkIds();
    const QList<qint64> pkgs = selectedPackageIds();
    if (links.isEmpty() && pkgs.isEmpty())
        return;
    IpcMessage req(IpcMsgType::RemoveGrabberLinks);
    req.append(toCbor(links));
    req.append(toCbor(pkgs));
    req.append(QString());
    send(std::move(req));
}

void LinkGrabberPanel::cleanup(const QString& mode)
{
    IpcMessage req(IpcMsgType::RemoveGrabberLinks);
    req.append(QCborArray{});
    req.append(QCborArray{});
    req.append(mode);
    send(std::move(req));
}

void LinkGrabberPanel::copyLinks()
{
    QStringList urls;
    for (const qint64 id : selectedLinkIds())
        urls << m_links.value(id).value(QStringLiteral("url")).toString();
    if (!urls.isEmpty())
        Clipboard::setText(urls.join(QLatin1Char('\n')));
}

void LinkGrabberPanel::openInBrowser()
{
    const QList<qint64> links = selectedLinkIds();
    if (links.size() > kOpenWithoutAsking
        && QMessageBox::question(this, tr("Open in Browser"), tr("Open %n links in the browser?", nullptr, int(links.size())))
               != QMessageBox::Yes)
        return;
    for (const qint64 id : links)
        QDesktopServices::openUrl(QUrl(m_links.value(id).value(QStringLiteral("url")).toString()));
}

void LinkGrabberPanel::send(IpcMessage req)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    QPointer<LinkGrabberPanel> self(this);
    m_ipc->sendRequest(std::move(req), [self](const IpcMessage& r) {
        if (self && r.isValid() && !r.fieldBool(0))
            QMessageBox::warning(self, tr("Link Grabber"), r.fieldMap(1).value(QStringLiteral("error")).toString());
    });
}

} // namespace eMule

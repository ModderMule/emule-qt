/// @file HosterPanel.cpp
/// @brief The Hoster tab — implementation.

#include "pch.h"
#include "panels/HosterPanel.h"
#include "utils/Log.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/StatusFilterBar.h"
#include "dialogs/HosterPuzzleDialog.h"
#include "panels/LinkGrabberPanel.h"
#include "utils/Clipboard.h"
#include "utils/ContainerDrop.h"
#include "utils/FileTypeIcons.h"
#include "utils/ListActivation.h"
#include "utils/MenuUtils.h"
#include "utils/StringUtils.h"
#include "IpcMessage.h"
#include "app/SkinIcons.h"

#include <QAction>
#include <QApplication>
#include <QCborArray>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <memory>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

enum Column { ColName, ColSize, ColProgress, ColSpeed, ColHoster, ColStatus, ColSource, ColCount };

/// ColName data: the item id (0 on a package row) and the package id of a package row
constexpr int kItemIdRole = Qt::UserRole + 1;
constexpr int kPackageIdRole = Qt::UserRole + 2;

// Ed3kHosterJoin::State
constexpr int kStateDownloading = 5;
constexpr int kStatePaused = 7;
constexpr int kStateComplete = 8;
/// a container sent to the core; DLCs are a few KiB (the core's cap)
constexpr qint64 kMaxContainerBytes = 4 * 1024 * 1024;
/// Ed3kHosterJoin::Reason
constexpr int kReasonUnpacked = 32;
constexpr int kReasonUnpackFailed = 33;
constexpr int kReasonUnpackPassword = 34;

/// The filter row's buttons. Persisted in UiState: append only.
enum Filter : int { FilterAll, FilterDownloading, FilterComplete, FilterPaused, FilterActive, FilterError, FilterCount };

QStringList filterNames()
{
    return {HosterPanel::tr("All"),    HosterPanel::tr("Downloading"), HosterPanel::tr("Complete"),
            HosterPanel::tr("Paused"), HosterPanel::tr("Active"),      HosterPanel::tr("Error")};
}

/// Whether item @p it belongs to filter @p filter; Error matches what Retry would act on.
bool matchesFilter(const QCborMap& it, int filter)
{
    const auto state = it.value(QStringLiteral("state")).toInteger();
    const auto reason = it.value(QStringLiteral("reason")).toInteger();
    switch (filter) {
    case FilterAll:
        return true;
    case FilterDownloading:   // resolving, queued, waiting, puzzle too: not done yet
        return state < kStatePaused;
    case FilterComplete:
        return state == kStateComplete;
    case FilterPaused:
        return state == kStatePaused;
    case FilterActive:
        return it.value(QStringLiteral("speed")).toInteger() > 0;
    case FilterError:
        return state > kStateComplete
            || (state == kStateComplete && (reason == kReasonUnpackFailed || reason == kReasonUnpackPassword));
    default:
        return false;
    }
}

/// Ed3kHosterJoin::State, worded here: IPC carries no localized text.
QString stateText(int state)
{
    switch (state) {
    case 0: return HosterPanel::tr("Finding a resolver");
    case 1: return HosterPanel::tr("Looking up in ed3k");
    case 2: return HosterPanel::tr("Checking");
    case 3: return HosterPanel::tr("Queued");
    case 4: return HosterPanel::tr("Waiting");
    case 5: return HosterPanel::tr("Downloading");
    case 6: return HosterPanel::tr("Puzzle");
    case 7: return HosterPanel::tr("Paused");
    case 8: return HosterPanel::tr("Complete");
    case 9: return HosterPanel::tr("File removed");
    case 10: return HosterPanel::tr("Needs you");
    case 11: return HosterPanel::tr("No resolver");
    case 12: return HosterPanel::tr("Failed");
    default: return {};
    }
}

/// Reason::HosterStep: what the resolver does before bytes flow
QString stepText(const QString& step)
{
    if (step == u"link")
        return HosterPanel::tr("Opening the link");
    if (step == u"hoster")
        return HosterPanel::tr("Talking to the hoster");
    if (step == u"hosterAccount")
        return HosterPanel::tr("Logging in, talking to the hoster");
    if (step == u"puzzle")
        return HosterPanel::tr("Solving a captcha");
    if (step == u"connect")
        return HosterPanel::tr("Connecting for the transfer");
    return step;
}

/// Ed3kHosterJoin::Reason; @p arg is technical and shown as it is.
QString reasonText(int reason, const QString& arg)
{
    switch (reason) {
    case 1: return HosterPanel::tr("Waiting for the ed3k node");
    case 2: return HosterPanel::tr("This build has no hoster resolver");
    case 3: return HosterPanel::tr("The resolver is not ready: %1").arg(arg);
    case 4: return HosterPanel::tr("No resolver plugin for %1").arg(arg);
    case 5: return HosterPanel::tr("Downloads from hosters are off (Options, ed3k, Hoster)");
    case 6: return HosterPanel::tr("The link was refused (%1)").arg(arg);
    case 7: return HosterPanel::tr("Busy");
    case 8: return HosterPanel::tr("Hoster timer");
    case 9: return HosterPanel::tr("Hoster quota");
    case 10: return HosterPanel::tr("Hoster busy");
    case 11: return HosterPanel::tr("The hoster wants a login (Options, ed3k, Hoster)");
    case 12: return HosterPanel::tr("The hoster wants a puzzle this resolver cannot show");
    case 13: return HosterPanel::tr("A puzzle, a login or a quota stands in the way");
    case 14: return HosterPanel::tr("The hoster ended the transfer early");
    case 15: return HosterPanel::tr("The hoster says the file was removed");
    case 16: return HosterPanel::tr("The hoster's file is not the one ed3k knows");
    case 17: return HosterPanel::tr("The hoster's file has another size");
    case 18: return HosterPanel::tr("Transfer failed");
    case 19: return HosterPanel::tr("Resolver error: %1").arg(arg);
    case 20: return HosterPanel::tr("Cannot write the file: %1").arg(arg);
    case 21: return HosterPanel::tr("Cannot move the file to %1").arg(arg);
    case 22: return HosterPanel::tr("Folder: %n link(s)", nullptr, arg.toInt());
    case 23: return HosterPanel::tr("From a link a peer named (%1)").arg(arg);
    case 24: return HosterPanel::tr("The hoster link is offline, no longer published");
    case 25: return stepText(arg);
    case 26: return HosterPanel::tr("Puzzle skipped (Retry asks again)");
    case 27: return HosterPanel::tr("The hoster refused the puzzle answer (Retry asks again)");
    case 28: return HosterPanel::tr("Puzzles of %1 are skipped (Retry asks again)").arg(arg);
    case 29: return HosterPanel::tr("The hoster wants a puzzle; puzzles are off (Options, ed3k, Hoster)");
    case 30: return HosterPanel::tr("Waiting for the other parts to extract");
    case 31: return HosterPanel::tr("Extracting %1%").arg(arg);
    case 32: return HosterPanel::tr("Extracted to %1").arg(arg);
    case 33: return HosterPanel::tr("Extraction failed: %1 (Retry tries again)").arg(arg);
    case 34: return HosterPanel::tr("The archive needs a password (Set Password…)");
    case 35: return HosterPanel::tr("Checking the file against the hoster's %1").arg(arg.toUpper());
    case 36: return HosterPanel::tr("The file does not match the hoster's %1, dropped (Retry downloads it again)").arg(arg.toUpper());
    default: return {};
    }
}

/// The folder "Open folder" shows: where an archive was extracted to, else the file's.
QString folderOf(const QCborMap& it)
{
    if (it.value(QStringLiteral("reason")).toInteger() == kReasonUnpacked)
        if (const QString dir = it.value(QStringLiteral("reasonArg")).toString(); !dir.isEmpty())
            return dir;
    return QFileInfo(it.value(QStringLiteral("file")).toString()).absolutePath();
}

/// Sorts numbers by the value kept in UserRole, text otherwise.
class HosterRow : public QTreeWidgetItem {
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

} // namespace

HosterPanel::HosterPanel(QWidget* parent)
    : QWidget(parent)
{
    // Downloads | Link Grabber, as the Kad window's tabs; one tab: no tab bar
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    outer->addWidget(m_tabs);
    auto* page = new QWidget(m_tabs);
    m_tabs->addTab(page, menuIcon("Hoster.ico"), tr("Downloads"));
    m_tabs->tabBar()->setVisible(false);
    m_grabber = new LinkGrabberPanel(this);
    m_grabber->hide();
    connect(m_grabber, &LinkGrabberPanel::countChanged, this, [this](int links, int crawling) {
        if (const int i = m_tabs->indexOf(m_grabber); i >= 0)
            m_tabs->setTabText(i, crawling > 0 ? tr("Link Grabber (%1, crawling)").arg(links)
                                               : tr("Link Grabber (%1)").arg(links));
    });

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* bar = new QHBoxLayout;
    auto* title = new QLabel(tr("Hoster downloads"), this);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    bar->addWidget(title);
    m_statusLabel = new QLabel(this);
    bar->addWidget(m_statusLabel, 1);
    auto* addBtn = new QPushButton(menuIcon("ListAdd.ico"), tr("Add Links..."), this);
    addBtn->setToolTip(tr("Links to files at hosters, one per line"));
    bar->addWidget(addBtn);
    auto* dlcBtn = new QPushButton(menuIcon("DLC.ico"), tr("Open DLC..."), this);
    dlcBtn->setToolTip(tr("Adds the links of a link container (.dlc, .rsdf, .ccf); you can also drop one here"));
    bar->addWidget(dlcBtn);
    layout->addLayout(bar);
    m_filterBar = new StatusFilterBar(filterNames(), theUiState.hosterFilter(), this);
    m_filterBar->layout()->setContentsMargins(0, 0, 0, 0);
    m_filterBar->setRowOn(theUiState.showHosterStatusFilter());
    m_filter = m_filterBar->activeFilter();
    connect(m_filterBar, &StatusFilterBar::filterChanged, this, &HosterPanel::setFilter);
    layout->addWidget(m_filterBar);
    updateFilterCounts();
    setAcceptDrops(true);

    m_tree = new ListTreeWidget(this);
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({tr("File Name"), tr("Size"), tr("Progress"), tr("Speed"), tr("Hoster"), tr("Status"),
                             tr("Source")});
    m_tree->setRootIsDecorated(true);   // packages hold their items
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(ColName, Qt::AscendingOrder);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree, 1);
    m_tree->bindColumns(QStringLiteral("hosterList"), {320});
    // After the restore: an older saved state pinned the name column to Stretch
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(true);

    m_addAction = new QAction(menuIcon("Hoster.ico"), tr("Add hoster links..."), this);
    connect(m_addAction, &QAction::triggered, this, [this] { promptAddLinks(); });
    connect(addBtn, &QPushButton::clicked, this, [this] { promptAddLinks(); });
    connect(dlcBtn, &QPushButton::clicked, this, &HosterPanel::promptOpenContainer);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &HosterPanel::showContextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &HosterPanel::openRow);

    // the Transfer list's keys
    ListKeyHandlers keys;
    keys.activate = [this](const QModelIndex& index) { openRow(m_tree->itemFromIndex(index)); };
    keys.remove = [this] {
        const int n = int(selectedIds().size());
        if (n > 0
            && QMessageBox::question(this, tr("Remove"), tr("Remove %n download(s) from the list?", nullptr, n),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                   == QMessageBox::Yes)
            act(QStringLiteral("remove"));
    };
    keys.copy = [this] { copyLinks(); };
    keys.paste = [this] {
        if (const QStringList links = clipboardLinks(); !links.isEmpty())
            pasteLinks(links);
    };
    keys.find = true;
    keys.expandKeys = true;
    keys.escapeDeselects = true;
    bindListKeys(m_tree, std::move(keys));
}

HosterPanel::~HosterPanel() = default;

void HosterPanel::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    if (!m_ipc)
        return;
    connect(m_ipc, &IpcClient::connected, this, &HosterPanel::refresh);
    connect(m_ipc, &IpcClient::hosterItemChanged, this, [this](const IpcMessage& msg) { upsert(msg.fieldMap(0)); });
    connect(m_ipc, &IpcClient::hosterItemRemoved, this, [this](const IpcMessage& msg) { removeRow(msg.field(0).toInteger()); });
    connect(m_ipc, &IpcClient::hosterPuzzleAsked, this, &HosterPanel::onPuzzle);
    connect(m_ipc, &IpcClient::hosterLinksExpanded, this, &HosterPanel::onLinks);
    connect(m_ipc, &IpcClient::ed3kStatusChanged, this, [this](const IpcMessage& msg) { applyStatus(msg.fieldMap(0)); });
    m_grabber->setIpcClient(ipc);
    if (m_ipc->isConnected())
        refresh();
}

void HosterPanel::grabLinks(const QString& text, const QString& source, bool clipboard)
{
    if (m_grabberOn) {
        m_grabber->grab(text, source, false, {}, {}, clipboard);
        return;
    }
    // the Link Grabber off: the links come back here and a dialog asks which to add
    if (!m_ipc || !m_ipc->isConnected())
        return;
    IpcMessage req(IpcMsgType::GrabLinks);
    req.append(text);
    req.append(source);
    req.append(false);
    req.append(QString());
    req.append(QCborArray{});
    req.append(true);   // ask
    req.append(clipboard);
    QPointer<HosterPanel> self(this);
    m_ipc->sendRequest(std::move(req), [self](const IpcMessage& r) {
        if (self && r.isValid() && r.fieldBool(0))
            self->m_grabTokens.insert(r.fieldMap(1).value(QStringLiteral("token")).toInteger());
    });
}

void HosterPanel::showLinkGrabber()
{
    if (const int i = m_tabs->indexOf(m_grabber); i >= 0)
        m_tabs->setCurrentIndex(i);
}

void HosterPanel::applyDisplayOptions()
{
    m_filterBar->setRowOn(theUiState.showHosterStatusFilter());
}

void HosterPanel::addLinks(const QStringList& links, const QString& package)
{
    QList<QStringList> files;
    for (const QString& link : links)
        files.append(QStringList{link});
    addLinks(files, package);
}

void HosterPanel::addLinks(const QList<QStringList>& files, const QString& package, qint64 packageId,
                           const QStringList& passwords)
{
    if (!m_ipc || files.isEmpty())
        return;
    QCborArray list;
    QStringList firsts;
    for (const QStringList& urls : files) {
        if (urls.isEmpty())
            continue;
        list.append(QCborArray::fromStringList(urls));
        firsts << urls.first();
    }
    if (list.isEmpty())
        return;
    IpcMessage req(IpcMsgType::AddHosterLinks);
    req.append(firsts.join(QLatin1Char('\n')));
    req.append(list);
    req.append(package.trimmed());
    req.append(packageId);
    req.append(QCborArray::fromStringList(passwords));
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& r) {
        if (!r.fieldBool(0))
            QMessageBox::warning(this, tr("Hoster links"), r.fieldMap(1).value(QStringLiteral("error")).toString());
    });
}

void HosterPanel::promptAddLinks(const QStringList& prefill)
{
    // "open all links in the Link Grabber": its own dialog (deep crawl, passwords)
    if (m_grabAll) {
        showLinkGrabber();
        m_grabber->promptAddLinks(prefill);
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add hoster links"));
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(tr("Links to files at hosters, one per line:"), &dlg));
    auto* edit = new QPlainTextEdit(prefill.join(QLatin1Char('\n')), &dlg);
    edit->setMinimumSize(520, 180);
    layout->addWidget(edit);
    // optional: the links download as one package, into a folder of that name
    auto* packageRowLayout = new QHBoxLayout;
    packageRowLayout->addWidget(new QLabel(tr("Package Name:"), &dlg));
    auto* packageEdit = new QLineEdit(&dlg);
    packageEdit->setPlaceholderText(tr("optional"));
    packageEdit->setToolTip(tr("The links download under one entry, into a folder of this name"));
    packageRowLayout->addWidget(packageEdit, 1);
    layout->addLayout(packageRowLayout);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QStringList links;
    for (const QString& line : edit->toPlainText().split(QLatin1Char('\n'))) {
        if (const QString t = line.trimmed(); !t.isEmpty())
            links << t;
    }
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Hoster links"), tr("Not connected to the eMule core."));
        return;
    }
    addLinks(links, packageEdit->text());
}

void HosterPanel::openContainerFiles(const QStringList& paths)
{
    if (paths.isEmpty())
        return;
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Open DLC"), tr("Not connected to the eMule core."));
        return;
    }
    for (const QString& path : paths) {
        QFile f(path);
        if (f.size() > kMaxContainerBytes || !f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Open DLC"), tr("Cannot read %1.").arg(QDir::toNativeSeparators(path)));
            continue;
        }
        openContainerData(QFileInfo(path).fileName(), f.readAll());
    }
}

void HosterPanel::openContainerData(const QString& name, const QByteArray& data)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Open DLC"), tr("Not connected to the eMule core."));
        return;
    }
    if (data.isEmpty() || data.size() > kMaxContainerBytes) {
        QMessageBox::warning(this, tr("Open DLC"), tr("Cannot read %1.").arg(name));
        return;
    }
    IpcMessage req(IpcMsgType::OpenHosterContainer);
    req.append(name);
    req.append(QCborValue(data));
    QPointer<HosterPanel> self(this);
    m_ipc->sendRequest(std::move(req), [self](const IpcMessage& r) {
        if (self && r.isValid() && !r.fieldBool(0))
            QMessageBox::warning(self, tr("Open DLC"), r.fieldMap(1).value(QStringLiteral("error")).toString());
    });
}

void HosterPanel::addClickNLoad(const ClickNLoadServer::Post& post, const ClickNLoadServer::Done& done)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        done(false, QStringLiteral("not connected to the eMule core"));
        return;
    }
    QPointer<HosterPanel> self(this);
    const auto answered = [self, done](const IpcMessage& r) {
        if (!r.isValid() || r.type() == IpcMsgType::Error || !r.fieldBool(0)) {
            // a refusal is a result with {error}; a request the core does not know, an Error
            done(false, r.type() == IpcMsgType::Error ? r.fieldString(1)
                                                      : r.fieldMap(1).value(QStringLiteral("error")).toString());
            return;
        }
        if (self)
            self->m_cnlTokens.insert(r.fieldMap(1).value(QStringLiteral("token")).toInteger());
        done(true, {});
    };
    if (post.kind == ClickNLoadServer::Post::Kind::Dlc) {
        IpcMessage req(IpcMsgType::OpenHosterContainer);
        req.append(QStringLiteral("clicknload.dlc"));
        req.append(QCborValue(post.crypted.toLatin1()));
        m_ipc->sendRequest(std::move(req), answered);
        return;
    }
    IpcMessage req(IpcMsgType::AddClickNLoad);
    req.append(post.urls);
    req.append(post.crypted);
    req.append(post.jk);
    req.append(post.k);
    req.append(post.package);
    req.append(QCborArray::fromStringList(post.passwords));
    req.append(post.source);
    m_ipc->sendRequest(std::move(req), answered);
}

void HosterPanel::promptOpenContainer()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Open DLC"), {},
                                                            gui::containerFileFilter() + QStringLiteral(";;")
                                                                + tr("All files (*)"));
    openContainerFiles(paths);
}

bool HosterPanel::acceptContainerDrop(const QMimeData* mime)
{
    const QStringList files = gui::containerDropFiles(mime);
    if (files.isEmpty())
        return false;
    // after the drop returns: the dialogs must not run inside the drag
    QMetaObject::invokeMethod(this, [this, files] { openContainerFiles(files); }, Qt::QueuedConnection);
    return true;
}

void HosterPanel::promptArchivePassword(const QList<qint64>& ids)
{
    // one item per package / folder: the core gives the password to the whole group
    QMap<QString, qint64> groups;
    for (const qint64 id : ids) {
        const QCborMap it = m_items.value(id);
        if (it.isEmpty())
            continue;
        const qint64 pkg = it.value(QStringLiteral("packageId")).toInteger();
        const QString key = pkg != 0 ? QStringLiteral("p%1").arg(pkg)
                                     : u'd' + QFileInfo(it.value(QStringLiteral("file")).toString()).absolutePath();
        if (!groups.contains(key))
            groups.insert(key, id);
    }
    if (groups.isEmpty())
        return;
    const QString key = groups.keys().join(u'|');
    if (QInputDialog* open = m_passwordAsks.value(key)) {
        open->raise();
        open->activateWindow();
        return;
    }
    QString label;
    if (groups.size() == 1) {
        const QCborMap it = m_items.value(groups.first());
        const bool pkg = it.value(QStringLiteral("packageId")).toInteger() != 0;
        label = tr("Archive password for \"%1\":")
                    .arg(it.value(pkg ? QStringLiteral("package") : QStringLiteral("name")).toString());
    } else {
        label = tr("Archive password for %n selected package(s):", nullptr, int(groups.size()));
    }
    auto* dlg = new QInputDialog(window());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // as the Usenet tab's Set Password
    dlg->setWindowTitle(tr("Set Password"));
    dlg->setLabelText(label);
    dlg->setInputMode(QInputDialog::TextInput);
    dlg->setTextEchoMode(QLineEdit::Password);
    const QList<qint64> targets = groups.values();
    connect(dlg, &QInputDialog::textValueSelected, this, [this, targets](const QString& text) {
        if (text.trimmed().isEmpty() || !m_ipc)
            return;
        // warn once, when none of them still wanted one
        auto failed = std::make_shared<int>(0);
        for (const qint64 id : targets) {
            IpcMessage req(IpcMsgType::HosterAction);
            req.append(id);
            req.append(QStringLiteral("password"));
            req.append(text);
            m_ipc->sendRequest(std::move(req), [this, failed, n = int(targets.size())](const IpcMessage& r) {
                if (r.isValid() && !r.fieldBool(0) && ++*failed == n)
                    QMessageBox::warning(this, tr("Set Password"), tr("The download no longer waits for a password."));
            });
        }
    });
    m_passwordAsks.insert(key, dlg);
    dlg->open();
}

void HosterPanel::refresh()
{
    if (!m_ipc)
        return;
    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetHosterItems), [this](const IpcMessage& r) {
        if (!r.fieldBool(0))
            return;
        const QCborMap res = r.fieldMap(1);
        m_tree->clear();
        m_rows.clear();
        m_packages.clear();
        m_items.clear();
        m_bulkFill = true;
        for (const QCborValue& v : res.value(QStringLiteral("items")).toArray())
            upsert(v.toMap());
        m_bulkFill = false;
        updateFilterCounts();
        applyStatus(QCborMap{{QStringLiteral("hoster"), res.value(QStringLiteral("status"))}});
    });
}

void HosterPanel::upsert(const QCborMap& item)
{
    const qint64 id = item.value(QStringLiteral("id")).toInteger();
    if (id == 0)
        return;
    const qint64 oldPackage = m_items.value(id).value(QStringLiteral("packageId")).toInteger();
    // an extraction that just ended wanting a password asks for one (not a refresh's rows)
    const bool askPassword = m_items.contains(id)
        && m_items.value(id).value(QStringLiteral("reason")).toInteger() != kReasonUnpackPassword
        && item.value(QStringLiteral("state")).toInteger() == kStateComplete
        && item.value(QStringLiteral("reason")).toInteger() == kReasonUnpackPassword;
    m_items[id] = item;
    const qint64 pkg = item.value(QStringLiteral("packageId")).toInteger();
    QTreeWidgetItem* parent = pkg != 0 ? packageRow(pkg, item.value(QStringLiteral("package")).toString())
                                       : nullptr;
    QTreeWidgetItem* row = m_rows.value(id);
    if (row && row->parent() != parent) {
        // moved between packages: take it out, put it back under the new parent
        if (QTreeWidgetItem* old = row->parent())
            old->removeChild(row);
        else
            m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(row));
        if (parent)
            parent->addChild(row);
        else
            m_tree->addTopLevelItem(row);
    }
    if (!row) {
        row = parent ? new HosterRow(parent) : new HosterRow(m_tree);
        row->setData(ColName, kItemIdRole, id);
        m_rows[id] = row;
    }
    fillRow(row, item);
    applyFilter(row);
    if (oldPackage != 0 && oldPackage != pkg)
        updatePackage(oldPackage);
    if (pkg != 0)
        updatePackage(pkg);
    if (!m_bulkFill)
        updateFilterCounts();
    if (askPassword)
        QMetaObject::invokeMethod(this, [this, id] { promptArchivePassword({id}); }, Qt::QueuedConnection);
}

void HosterPanel::removeRow(qint64 id)
{
    const qint64 pkg = m_items.take(id).value(QStringLiteral("packageId")).toInteger();
    delete m_rows.take(id);
    if (pkg != 0)
        updatePackage(pkg);
    updateFilterCounts();
}

QTreeWidgetItem* HosterPanel::packageRow(qint64 id, const QString& name)
{
    if (QTreeWidgetItem* row = m_packages.value(id))
        return row;
    auto* row = new HosterRow(m_tree);
    row->setData(ColName, kItemIdRole, 0);
    row->setData(ColName, kPackageIdRole, id);
    row->setText(ColName, name);
    static const QIcon folder = skinIcon(QStringLiteral(":/icons/FolderOpen.ico"));
    row->setIcon(ColName, folder);
    row->setToolTip(ColName, name);   // starts collapsed
    m_packages[id] = row;
    return row;
}

void HosterPanel::updatePackage(qint64 id)
{
    QTreeWidgetItem* row = m_packages.value(id);
    if (!row)
        return;
    if (row->childCount() == 0) {
        delete m_packages.take(id);
        return;
    }
    qint64 size = 0;
    qint64 done = 0;
    qint64 speed = 0;
    int complete = 0;
    int downloading = 0;
    QStringList hosts;
    for (int i = 0; i < row->childCount(); ++i) {
        const QCborMap it = m_items.value(row->child(i)->data(ColName, kItemIdRole).toLongLong());
        const qint64 itemSize = it.value(QStringLiteral("size")).toInteger();
        const int state = int(it.value(QStringLiteral("state")).toInteger());
        size += itemSize;
        done += state == kStateComplete ? itemSize : it.value(QStringLiteral("done")).toInteger();
        speed += it.value(QStringLiteral("speed")).toInteger();
        complete += state == kStateComplete ? 1 : 0;
        downloading += state == kStateDownloading ? 1 : 0;
        if (const QString host = it.value(QStringLiteral("host")).toString();
            !host.isEmpty() && !hosts.contains(host))
            hosts << host;
    }
    const int total = row->childCount();
    const double pct = complete == total ? 100.0 : (size > 0 ? 100.0 * double(done) / double(size) : 0.0);
    row->setText(ColSize, size > 0 ? formatByteSize(size) : QString());
    row->setData(ColSize, Qt::UserRole, size);
    row->setText(ColProgress, QStringLiteral("%1%").arg(pct, 0, 'f', 1));
    row->setData(ColProgress, Qt::UserRole, pct);
    row->setText(ColSpeed, speed > 0 ? formatByteRate(double(speed)) : QString());
    row->setData(ColSpeed, Qt::UserRole, speed);
    row->setText(ColHoster, hosts.size() <= 2 ? hosts.join(QStringLiteral(", "))
                                              : tr("%n hoster(s)", nullptr, int(hosts.size())));
    row->setToolTip(ColHoster, hosts.join(QLatin1Char('\n')));
    QString status = complete == total ? stateText(kStateComplete)
                                       : tr("%1 of %2 complete").arg(complete).arg(total);
    if (downloading > 0 && complete < total)
        status = stateText(kStateDownloading) + QStringLiteral(", ") + status;
    row->setText(ColStatus, status);
    row->setToolTip(ColStatus, status);
    syncPackageVisibility(row);
}

void HosterPanel::fillRow(QTreeWidgetItem* row, const QCborMap& it)
{
    const qint64 size = it.value(QStringLiteral("size")).toInteger();
    const qint64 done = it.value(QStringLiteral("done")).toInteger();
    const qint64 speed = it.value(QStringLiteral("speed")).toInteger();
    const int state = int(it.value(QStringLiteral("state")).toInteger());
    const double pct = state == kStateComplete ? 100.0 : (size > 0 ? 100.0 * double(done) / double(size) : 0.0);
    QString name = it.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = it.value(QStringLiteral("url")).toString();
    row->setText(ColName, name);
    row->setIcon(ColName, fileTypeIconForName(name));   // as the Transfer window
    row->setToolTip(ColName, it.value(QStringLiteral("url")).toString());
    row->setText(ColSize, size > 0 ? formatByteSize(size) : QString());
    row->setData(ColSize, Qt::UserRole, size);
    row->setText(ColProgress, QStringLiteral("%1%").arg(pct, 0, 'f', 1));
    row->setData(ColProgress, Qt::UserRole, pct);
    row->setText(ColSpeed, speed > 0 ? formatByteRate(double(speed)) : QString());
    row->setData(ColSpeed, Qt::UserRole, speed);
    row->setText(ColHoster, it.value(QStringLiteral("host")).toString());
    const qint64 mirrorNo = it.value(QStringLiteral("mirrorNo")).toInteger();
    const qint64 mirrorCount = mirrorNo + 1 + it.value(QStringLiteral("mirrors")).toInteger();
    row->setToolTip(ColHoster, mirrorCount > 1 ? tr("Mirror %1 of %2").arg(mirrorNo + 1).arg(mirrorCount) : QString());
    QString status = stateText(state);
    const QString error = reasonText(int(it.value(QStringLiteral("reason")).toInteger()),
                                     it.value(QStringLiteral("reasonArg")).toString());
    if (const qint64 until = it.value(QStringLiteral("waitUntil")).toInteger(); until > 0) {
        const qint64 left = (until - QDateTime::currentMSecsSinceEpoch()) / 1000;
        if (left > 0)
            status += QStringLiteral(" (%1)").arg(formatDuration(std::chrono::seconds(left)));
    }
    if (!error.isEmpty())
        status += QStringLiteral(": ") + error;
    row->setText(ColStatus, status);
    row->setToolTip(ColStatus, status);
    row->setText(ColSource, it.value(QStringLiteral("verified")).toBool() ? tr("ed3k + link") : tr("link"));
    row->setToolTip(ColSource, it.value(QStringLiteral("verified")).toBool()
                                   ? tr("Known in ed3k: every block is verified, peers help")
                                   : tr("Unknown in ed3k: checked as a whole when complete"));
}

void HosterPanel::showContextMenu(const QPoint& pos)
{
    const QList<qint64> ids = selectedIds();
    QMenu menu(this);
    QAction* pause = menu.addAction(menuIcon("Pause.ico"), tr("Pause"));
    QAction* resume = menu.addAction(menuIcon("Start.ico"), tr("Resume"));
    // from the first link again; skipped puzzles are asked again
    QAction* retry = menu.addAction(menuIcon("Restore.ico"), tr("Retry"));
    QAction* password = menu.addAction(menuIcon("Security.ico"), tr("Set Password…"));
    menu.addSeparator();
    QAction* remove = menu.addAction(menuIcon("ListRemove.ico"), tr("Remove"));
    QAction* del = menu.addAction(menuIcon("Delete.ico"), tr("Remove and delete file"));
    menu.addSeparator();
    QAction* copy = menu.addAction(menuIcon("Copy.ico"), tr("Copy link"));
    QAction* folder = menu.addAction(menuIcon("FolderOpen.ico"), tr("Open folder"));
    menu.addSeparator();
    QAction* add = menu.addAction(menuIcon("ListAdd.ico"), tr("Add Links..."));
    QAction* dlc = menu.addAction(menuIcon("DLC.ico"), tr("Open DLC..."));
    // straight from the clipboard, no dialog
    const QStringList pasted = clipboardLinks();
    QAction* paste = menu.addAction(menuIcon("eD2kLinkPaste.ico"), tr("Paste links"));
    paste->setEnabled(!pasted.isEmpty() && m_ipc && m_ipc->isConnected());
    for (QAction* a : {pause, resume, remove, del, copy, folder})
        a->setEnabled(!ids.isEmpty());
    // the folder of the first complete one; a package's is its own folder
    QString folderDir;
    for (const qint64 id : ids) {
        const QCborMap it = m_items.value(id);
        if (it.value(QStringLiteral("state")).toInteger() == kStateComplete
            && !it.value(QStringLiteral("file")).toString().isEmpty()) {
            folderDir = folderOf(it);
            break;
        }
    }
    folder->setEnabled(!folderDir.isEmpty());
    // Waiting, Gone, Needs you, No resolver, Failed; a failed extraction
    retry->setEnabled(std::ranges::any_of(ids, [this](qint64 id) {
        const QCborMap it = m_items.value(id);
        const auto s = it.value(QStringLiteral("state")).toInteger();
        const auto r = it.value(QStringLiteral("reason")).toInteger();
        return s == 4 || s >= 9 || (s == kStateComplete && (r == kReasonUnpackFailed || r == kReasonUnpackPassword));
    }));
    // an encrypted set, or a failed one (some tools say only "cannot open"); all selected
    QList<qint64> passwordIds;
    for (const qint64 id : ids) {
        const QCborMap it = m_items.value(id);
        const auto r = it.value(QStringLiteral("reason")).toInteger();
        if (it.value(QStringLiteral("state")).toInteger() == kStateComplete
            && (r == kReasonUnpackPassword || r == kReasonUnpackFailed))
            passwordIds << id;
    }
    password->setEnabled(!passwordIds.isEmpty());
    if (ids.size() == 1) {
        const int state = int(m_items.value(ids.first()).value(QStringLiteral("state")).toInteger());
        pause->setEnabled(state != kStateComplete && state != kStatePaused);
        resume->setEnabled(state != kStateComplete && state != 3 && state != 4 && state != 5);
    }
    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (chosen == pause)
        act(QStringLiteral("pause"));
    else if (chosen == resume)
        act(QStringLiteral("resume"));
    else if (chosen == retry)
        act(QStringLiteral("retry"));
    else if (chosen == password)
        promptArchivePassword(passwordIds);
    else if (chosen == remove)
        act(QStringLiteral("remove"));
    else if (chosen == del) {
        if (QMessageBox::question(this, tr("Remove and delete file"), tr("Delete the selected files from disk?"))
            == QMessageBox::Yes)
            act(QStringLiteral("delete"));
    } else if (chosen == copy) {
        copyLinks();
    } else if (chosen == folder && !folderDir.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folderDir));
    } else if (chosen == add) {
        promptAddLinks();
    } else if (chosen == dlc) {
        promptOpenContainer();
    } else if (chosen == paste) {
        pasteLinks(pasted);
    }
}

void HosterPanel::act(const QString& op)
{
    if (!m_ipc)
        return;
    for (const qint64 id : selectedIds()) {
        IpcMessage req(IpcMsgType::HosterAction);
        req.append(id);
        req.append(op);
        m_ipc->sendRequest(std::move(req));
    }
}

void HosterPanel::onPuzzle(const IpcMessage& msg)
{
    const qint64 item = msg.field(0).toInteger();
    const quint64 puzzle = quint64(msg.field(1).toInteger());
    const QByteArray image = msg.field(2).toByteArray();
    QString name = m_items.value(item).value(QStringLiteral("name")).toString();
    auto* dlg = new HosterPuzzleDialog(image, msg.fieldString(3), name, msg.fieldString(4), int(msg.field(5).toInteger()),
                                       window());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &HosterPuzzleDialog::answered, this, [this, puzzle, dlg](const QString& text, bool skipHoster) {
        m_puzzleDialogs.removeAll(dlg);
        if (skipHoster) {
            // the core skips the hoster's other open puzzles; their dialogs go too
            for (const QPointer<HosterPuzzleDialog>& other : std::as_const(m_puzzleDialogs))
                if (other && other->host() == dlg->host())
                    QMetaObject::invokeMethod(other.data(), &QDialog::reject, Qt::QueuedConnection);
        }
        if (!m_ipc)
            return;
        IpcMessage req(IpcMsgType::AnswerHosterPuzzle);
        req.append(qint64(puzzle));
        req.append(text);
        req.append(skipHoster);
        m_ipc->sendRequest(std::move(req));
    });
    m_puzzleDialogs.removeAll(nullptr);
    m_puzzleDialogs << dlg;
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void HosterPanel::onLinks(const IpcMessage& msg)
{
    const QCborArray links = msg.field(1).toArray();
    if (msg.field(0).toInteger() == 0 && msg.field(2).isMap()
        && msg.fieldMap(2).value(QStringLiteral("grabbed")).toBool()) {
        // the core put them into the Link Grabber: show it, no dialog
        const QCborMap c = msg.fieldMap(2);
        const bool cnl = c.value(QStringLiteral("cnl")).toBool();
        const bool ours = m_cnlTokens.remove(c.value(QStringLiteral("token")).toInteger());
        if (cnl && !ours)
            return;
        if (ours)
            emit clickNLoadArrived();
        showLinkGrabber();
        return;
    }
    if (msg.field(0).toInteger() == 0 && msg.field(2).isMap()) {
        // its dialog is modal: run it outside the IPC socket's readyRead, which Qt does not
        // emit again while inside it, so no reply (next Click'n'Load post) could arrive
        QMetaObject::invokeMethod(this, [this, c = msg.fieldMap(2), links] { onContainer(c, links); },
                                  Qt::QueuedConnection);
        return;
    }
    if (links.isEmpty())
        return;
    // the folder becomes a package; inside one already, the user picks (never nested)
    const QCborMap folderItem = m_items.value(msg.field(0).toInteger());
    QString folderName = folderItem.value(QStringLiteral("name")).toString();
    if (folderName.isEmpty())
        folderName = QUrl(folderItem.value(QStringLiteral("url")).toString()).fileName();
    const qint64 inPackage = folderItem.value(QStringLiteral("packageId")).toInteger();
    const QString inPackageName = folderItem.value(QStringLiteral("package")).toString();
    QDialog dlg(window());
    dlg.setWindowTitle(tr("Links of a folder"));
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(tr("The link names a folder. Add which of its files?"), &dlg));
    auto* list = new QListWidget(&dlg);
    for (const QCborValue& v : links) {
        const QCborMap l = v.toMap();
        const qint64 size = l.value(QStringLiteral("size")).toInteger();
        QString text = l.value(QStringLiteral("name")).toString();
        if (text.isEmpty())
            text = l.value(QStringLiteral("url")).toString();
        if (size > 0)
            text += QStringLiteral(" (%1)").arg(formatByteSize(size));
        auto* row = new QListWidgetItem(text, list);
        row->setData(Qt::UserRole, l.value(QStringLiteral("url")).toString());
        row->setToolTip(l.value(QStringLiteral("url")).toString());
        row->setCheckState(Qt::Checked);
    }
    list->setMinimumSize(480, 220);
    layout->addWidget(list);
    QRadioButton* joinPackage = nullptr;
    if (inPackage != 0) {
        joinPackage = new QRadioButton(tr("Add to the package \"%1\"").arg(inPackageName), &dlg);
        auto* newPackage = new QRadioButton(
            folderName.isEmpty() ? tr("Add as a new package") : tr("Add as a new package \"%1\"").arg(folderName),
            &dlg);
        joinPackage->setChecked(true);
        layout->addWidget(joinPackage);
        layout->addWidget(newPackage);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QList<QStringList> files;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            files.append(QStringList{list->item(i)->data(Qt::UserRole).toString()});
    if (joinPackage && joinPackage->isChecked())
        addLinks(files, {}, inPackage);
    else
        addLinks(files, folderName.isEmpty() ? tr("Folder") : folderName);
}

void HosterPanel::applyStatus(const QCborMap& st)
{
    const QCborMap h = st.value(QStringLiteral("hoster")).toMap();
    if (h.isEmpty())
        return;
    if (const bool on = h.value(QStringLiteral("enabled")).toBool(); on != m_enabled) {
        m_enabled = on;
        emit hostersEnabledChanged(on);
    }
    m_grabAll = h.value(QStringLiteral("grabAll")).toBool();
    m_grabber->setDeepCrawl(h.value(QStringLiteral("deepCrawl")).toBool());
    setGrabberOn(h.value(QStringLiteral("linkGrabber")).toBool());
    if (const bool watch = h.value(QStringLiteral("clipboard")).toBool(); watch != m_watchClipboard) {
        m_watchClipboard = watch;
        emit clipboardWatchChanged(watch);
    }
    QString text;
    if (!m_enabled)
        text = tr("Downloads from hosters are off (Options, ed3k, Hoster).");
    else if (const QCborMap r = h.value(QStringLiteral("resolver")).toMap(); !r.value(QStringLiteral("available")).toBool())
        text = tr("Resolver not ready: %1").arg(r.value(QStringLiteral("reason")).toString());
    else if (text = r.value(QStringLiteral("description")).toString(); text.isEmpty())
        // the sidecar not started yet: the installed engine's version
        if (const QString v = r.value(QStringLiteral("version")).toString(); !v.isEmpty())
            text = tr("JDownloader %1").arg(v);
    m_statusLabel->setText(text);
}

QList<qint64> HosterPanel::selectedIds() const
{
    // a package row stands for its items; an item selected with its package counts once
    QList<qint64> out;
    const auto take = [&out](const QTreeWidgetItem* row) {
        if (const qint64 id = row->data(ColName, kItemIdRole).toLongLong(); id != 0 && !out.contains(id))
            out << id;
    };
    for (QTreeWidgetItem* row : m_tree->selectedItems()) {
        take(row);
        // a package acts on its items the filter shows
        for (int i = 0; i < row->childCount(); ++i)
            if (!row->child(i)->isHidden())
                take(row->child(i));
    }
    return out;
}

QStringList HosterPanel::clipboardLinks()
{
    QStringList links;
    const QString text = QApplication::clipboard()->text();
    for (const QStringView line : QStringView(text).split(QLatin1Char('\n'))) {
        const QString t = line.trimmed().toString();
        if (t.startsWith(QLatin1StringView("http://"), Qt::CaseInsensitive)
            || t.startsWith(QLatin1StringView("https://"), Qt::CaseInsensitive))
            links << t;
    }
    return links;
}

void HosterPanel::openRow(QTreeWidgetItem* row)
{
    if (!row)
        return;
    const QCborMap it = m_items.value(row->data(ColName, kItemIdRole).toLongLong());
    if (it.value(QStringLiteral("state")).toInteger() == kStateComplete)
        QDesktopServices::openUrl(QUrl::fromLocalFile(folderOf(it)));
}

void HosterPanel::copyLinks()
{
    QStringList urls;
    for (const qint64 id : selectedIds())
        urls << m_items.value(id).value(QStringLiteral("url")).toString();
    if (!urls.isEmpty())
        Clipboard::setText(urls.join(QLatin1Char('\n')));
}

void HosterPanel::pasteLinks(const QStringList& links)
{
    if (m_grabAll) {
        showLinkGrabber();
        m_grabber->grab(links.join(QLatin1Char('\n')), tr("Clipboard"));
        return;
    }
    addLinks(links);
}

void HosterPanel::setGrabberOn(bool on)
{
    if (on == m_grabberOn)
        return;
    m_grabberOn = on;
    if (on) {
        m_tabs->addTab(m_grabber, menuIcon("ListAdd.ico"), tr("Link Grabber (%1)").arg(m_grabber->linkCount()));
    } else if (const int i = m_tabs->indexOf(m_grabber); i >= 0) {
        m_tabs->removeTab(i);
        m_grabber->hide();
    }
    m_tabs->tabBar()->setVisible(m_tabs->count() > 1);
}

void HosterPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!gui::containerDropFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void HosterPanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (!gui::containerDropFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void HosterPanel::dropEvent(QDropEvent* event)
{
    if (acceptContainerDrop(event->mimeData()))
        event->acceptProposedAction();
}

void HosterPanel::onContainer(const QCborMap& c, const QCborArray& links)
{
    // a copied link (GrabLinks ask): only the GUI that sent it asks, none found: nothing
    if (c.value(QStringLiteral("grab")).toBool()) {
        if (!m_grabTokens.remove(c.value(QStringLiteral("token")).toInteger()) || links.isEmpty())
            return;
    }
    const bool grab = c.value(QStringLiteral("grab")).toBool();
    // Click'n'Load: only the GUI the page posted to asks (pushes reach every GUI)
    const bool cnl = c.value(QStringLiteral("cnl")).toBool() || grab;
    const bool ours = m_cnlTokens.remove(c.value(QStringLiteral("token")).toInteger());
    if (cnl && !ours && !grab)
        return;
    if (ours)
        emit clickNLoadArrived();
    QString fileName = c.value(QStringLiteral("fileName")).toString();
    if (grab) {
        fileName = tr("the clipboard");
    } else if (cnl) {
        const QString source = c.value(QStringLiteral("source")).toString();
        const QString host = QUrl(source).host();
        fileName = !host.isEmpty() ? host : !source.isEmpty() ? source.left(80) : tr("Click'n'Load");
    }
    const QString title = cnl ? tr("Click'n'Load") : tr("Open DLC");
    if (cnl && links.isEmpty()) {
        const QString code = c.value(QStringLiteral("code")).toString();
        QString why = code == QLatin1StringView("unavailable")
                          ? tr("The hoster resolver is not ready.")
                          : tr("The page sent no links that can be downloaded, or they could not be decrypted.");
        if (const QString text = c.value(QStringLiteral("text")).toString(); !text.isEmpty())
            why += QStringLiteral("\n\n") + text;
        QMessageBox::warning(window(), title, tr("%1: %2").arg(fileName, why));
        return;
    }
    if (links.isEmpty()) {
        const QString code = c.value(QStringLiteral("code")).toString();
        const QString text = c.value(QStringLiteral("text")).toString();
        QString why;
        if (code == QLatin1StringView("limit"))
            why = tr("The DLC key service refuses for now: too many containers were opened. Try again in a few minutes.");
        else if (code == QLatin1StringView("invalid"))
            why = tr("This is no valid link container, or it holds no links.");
        else if (code == QLatin1StringView("password"))
            why = tr("The container is protected by a password.");
        else if (code == QLatin1StringView("noPlugin"))
            why = tr("No plugin reads this kind of file.");
        else if (code == QLatin1StringView("bad"))
            why = tr("The installed hoster resolver cannot read link containers yet; a newer eMule Qt brings one.");
        else if (code == QLatin1StringView("unavailable"))
            why = tr("The hoster resolver is not ready.");
        else
            why = tr("The container could not be read.");
        if (!text.isEmpty() && code != QLatin1StringView("bad"))
            why += QStringLiteral("\n\n") + text;
        QMessageBox::warning(window(), title, tr("%1: %2").arg(fileName, why));
        return;
    }

    // a container may hold several packages: one per name, in the container's order
    QStringList packages;
    for (const QCborValue& v : links)
        if (const QString p = v.toMap().value(QStringLiteral("package")).toString(); !packages.contains(p))
            packages << p;
    const bool single = packages.size() == 1;

    if (cnl)
        logInfo(QStringLiteral("%1: %2 link(s) from %3, asking which to add")
                    .arg(grab ? QStringLiteral("Clipboard") : QStringLiteral("Click'n'Load"))
                    .arg(links.size())
                    .arg(fileName));
    QDialog dlg(window());
    dlg.setWindowTitle(grab ? tr("Copied hoster links") : tr("Links of %1").arg(fileName));
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(grab  ? tr("You copied %n hoster link(s). Add which?", nullptr, int(links.size()))
                                 : cnl ? tr("%1 sent %n link(s) (Click'n'Load). Add which?", nullptr, int(links.size())).arg(fileName)
                                       : tr("The container holds %n link(s). Add which?", nullptr, int(links.size())),
                                 &dlg));
    auto* list = new QListWidget(&dlg);
    for (const QCborValue& v : links) {
        const QCborMap l = v.toMap();
        const QString url = l.value(QStringLiteral("url")).toString();
        QString text = l.value(QStringLiteral("name")).toString();
        if (text.isEmpty())
            text = url;
        if (const qint64 size = l.value(QStringLiteral("size")).toInteger(); size > 0)
            text += QStringLiteral(" (%1)").arg(formatByteSize(size));
        if (const QString p = l.value(QStringLiteral("package")).toString(); !single && !p.isEmpty())
            text = QStringLiteral("[%1] %2").arg(p, text);
        auto* row = new QListWidgetItem(fileTypeIconForName(l.value(QStringLiteral("name")).toString()), text, list);
        row->setData(Qt::UserRole, url);
        row->setToolTip(url);
        row->setCheckState(Qt::Checked);
    }
    list->setMinimumSize(520, 240);
    layout->addWidget(list);
    QLineEdit* packageEdit = nullptr;
    if (single) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Package Name:"), &dlg));
        const QString fallback = grab ? QString() : cnl ? fileName : QFileInfo(fileName).completeBaseName();
        packageEdit = new QLineEdit(packages.first().isEmpty() ? fallback : packages.first(), &dlg);
        packageEdit->setToolTip(tr("The links download under one entry, into a folder of this name"));
        row->addWidget(packageEdit, 1);
        layout->addLayout(row);
    }
    bool anyPassword = false;
    for (const QCborValue& v : links)
        anyPassword = anyPassword || !v.toMap().value(QStringLiteral("passwords")).toArray().isEmpty();
    if (anyPassword) {
        auto* note = new QLabel(cnl ? tr("The page names archive passwords; extraction tries them.")
                                    : tr("The container names archive passwords; extraction tries them."),
                                &dlg);
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;

    for (const QString& pkg : std::as_const(packages)) {
        QList<QStringList> files;
        QStringList passwords;
        for (int i = 0; i < list->count(); ++i) {
            const QCborMap l = links.at(i).toMap();
            if (list->item(i)->checkState() != Qt::Checked || l.value(QStringLiteral("package")).toString() != pkg)
                continue;
            files.append(QStringList{list->item(i)->data(Qt::UserRole).toString()});
            for (const QCborValue& pw : l.value(QStringLiteral("passwords")).toArray())
                if (const QString t = pw.toString(); !t.isEmpty() && !passwords.contains(t))
                    passwords << t;
        }
        QString name = packageEdit ? packageEdit->text().trimmed() : pkg;
        if (name.isEmpty() && !grab)
            name = cnl ? fileName : QFileInfo(fileName).completeBaseName();
        addLinks(files, name, 0, passwords);
    }
}

void HosterPanel::setFilter(int filter)
{
    m_filter = std::clamp(filter, 0, int(FilterCount) - 1);
    theUiState.setHosterFilter(m_filterBar->current());
    for (QTreeWidgetItem* row : std::as_const(m_rows))
        applyFilter(row);
    for (QTreeWidgetItem* row : std::as_const(m_packages))
        syncPackageVisibility(row);
}

void HosterPanel::applyFilter(QTreeWidgetItem* row)
{
    const bool hide = !matchesFilter(m_items.value(row->data(ColName, kItemIdRole).toLongLong()), m_filter);
    if (hide)
        row->setSelected(false);
    row->setHidden(hide);
}

void HosterPanel::syncPackageVisibility(QTreeWidgetItem* row)
{
    bool shown = false;
    for (int i = 0; i < row->childCount() && !shown; ++i)
        shown = !row->child(i)->isHidden();
    if (!shown)
        row->setSelected(false);
    row->setHidden(!shown);
}

void HosterPanel::updateFilterCounts()
{
    std::array<int, FilterCount> counts{};
    for (const QCborMap& it : std::as_const(m_items))
        for (int f = 0; f < FilterCount; ++f)
            if (matchesFilter(it, f))
                ++counts[std::size_t(f)];
    m_filterBar->setCounts(counts);
}

} // namespace eMule

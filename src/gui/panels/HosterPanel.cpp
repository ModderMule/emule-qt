/// @file HosterPanel.cpp
/// @brief The Hoster tab — implementation.

#include "pch.h"
#include "panels/HosterPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "dialogs/HosterPuzzleDialog.h"
#include "utils/MenuUtils.h"
#include "utils/StringUtils.h"
#include "IpcMessage.h"

#include <QAction>
#include <QApplication>
#include <QCborArray>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

enum Column { ColName, ColSize, ColProgress, ColSpeed, ColHoster, ColStatus, ColSource, ColCount };

// Ed3kHosterJoin::State
constexpr int kStatePaused = 7;
constexpr int kStateComplete = 8;

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
    default: return {};
    }
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
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* bar = new QHBoxLayout;
    auto* title = new QLabel(tr("Hoster downloads"), this);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    bar->addWidget(title);
    m_statusLabel = new QLabel(this);
    bar->addWidget(m_statusLabel, 1);
    auto* addBtn = new QPushButton(tr("Add links..."), this);
    auto* optBtn = new QPushButton(tr("Options..."), this);
    bar->addWidget(addBtn);
    bar->addWidget(optBtn);
    layout->addLayout(bar);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({tr("File Name"), tr("Size"), tr("Progress"), tr("Speed"), tr("Hoster"), tr("Status"),
                             tr("Source")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(ColName, Qt::AscendingOrder);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    layout->addWidget(m_tree, 1);
    theUiState.bindHeaderView(m_tree->header(), QStringLiteral("hosterList"));

    m_addAction = new QAction(menuIcon("Hoster.ico"), tr("Add hoster links..."), this);
    connect(m_addAction, &QAction::triggered, this, [this] { promptAddLinks(); });
    connect(addBtn, &QPushButton::clicked, this, [this] { promptAddLinks(); });
    connect(optBtn, &QPushButton::clicked, this, &HosterPanel::optionsRequested);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &HosterPanel::showContextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* row) {
        const QCborMap it = m_items.value(row->data(ColName, Qt::UserRole + 1).toLongLong());
        if (it.value(QStringLiteral("state")).toInteger() == kStateComplete)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(it.value(QStringLiteral("file")).toString()).absolutePath()));
    });
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
    if (m_ipc->isConnected())
        refresh();
}

void HosterPanel::addLinks(const QStringList& links)
{
    if (!m_ipc || links.isEmpty())
        return;
    IpcMessage req(IpcMsgType::AddHosterLinks);
    req.append(links.join(QLatin1Char('\n')));
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& r) {
        if (!r.fieldBool(0))
            QMessageBox::warning(this, tr("Hoster links"), r.fieldMap(1).value(QStringLiteral("error")).toString());
    });
}

void HosterPanel::promptAddLinks(const QStringList& prefill)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add hoster links"));
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(tr("Links to files at hosters, one per line:"), &dlg));
    auto* edit = new QPlainTextEdit(prefill.join(QLatin1Char('\n')), &dlg);
    edit->setMinimumSize(520, 180);
    layout->addWidget(edit);
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
    addLinks(links);
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
        m_items.clear();
        for (const QCborValue& v : res.value(QStringLiteral("items")).toArray())
            upsert(v.toMap());
        applyStatus(QCborMap{{QStringLiteral("hoster"), res.value(QStringLiteral("status"))}});
    });
}

void HosterPanel::upsert(const QCborMap& item)
{
    const qint64 id = item.value(QStringLiteral("id")).toInteger();
    if (id == 0)
        return;
    m_items[id] = item;
    QTreeWidgetItem* row = m_rows.value(id);
    if (!row) {
        row = new HosterRow(m_tree);
        row->setData(ColName, Qt::UserRole + 1, id);
        m_rows[id] = row;
    }
    fillRow(row, item);
}

void HosterPanel::removeRow(qint64 id)
{
    m_items.remove(id);
    delete m_rows.take(id);
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
    row->setToolTip(ColName, it.value(QStringLiteral("url")).toString());
    row->setText(ColSize, size > 0 ? formatByteSize(size) : QString());
    row->setData(ColSize, Qt::UserRole, size);
    row->setText(ColProgress, QStringLiteral("%1%").arg(pct, 0, 'f', 1));
    row->setData(ColProgress, Qt::UserRole, pct);
    row->setText(ColSpeed, speed > 0 ? formatByteRate(double(speed)) : QString());
    row->setData(ColSpeed, Qt::UserRole, speed);
    row->setText(ColHoster, it.value(QStringLiteral("host")).toString());
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
    menu.addSeparator();
    QAction* remove = menu.addAction(menuIcon("ListRemove.ico"), tr("Remove"));
    QAction* del = menu.addAction(menuIcon("Delete.ico"), tr("Remove and delete file"));
    menu.addSeparator();
    QAction* copy = menu.addAction(menuIcon("Copy.ico"), tr("Copy link"));
    QAction* folder = menu.addAction(menuIcon("FolderOpen.ico"), tr("Open folder"));
    menu.addSeparator();
    QAction* add = menu.addAction(menuIcon("ListAdd.ico"), tr("Add links..."));
    for (QAction* a : {pause, resume, remove, del, copy, folder})
        a->setEnabled(!ids.isEmpty());
    if (ids.size() == 1) {
        const int state = int(m_items.value(ids.first()).value(QStringLiteral("state")).toInteger());
        pause->setEnabled(state != kStateComplete && state != kStatePaused);
        resume->setEnabled(state != kStateComplete && state != 3 && state != 4 && state != 5);
        folder->setEnabled(state == kStateComplete);
    }
    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (chosen == pause)
        act(QStringLiteral("pause"));
    else if (chosen == resume)
        act(QStringLiteral("resume"));
    else if (chosen == remove)
        act(QStringLiteral("remove"));
    else if (chosen == del) {
        if (QMessageBox::question(this, tr("Remove and delete file"), tr("Delete the selected files from disk?"))
            == QMessageBox::Yes)
            act(QStringLiteral("delete"));
    } else if (chosen == copy) {
        QStringList urls;
        for (const qint64 id : ids)
            urls << m_items.value(id).value(QStringLiteral("url")).toString();
        QApplication::clipboard()->setText(urls.join(QLatin1Char('\n')));
    } else if (chosen == folder && !ids.isEmpty()) {
        const QString file = m_items.value(ids.first()).value(QStringLiteral("file")).toString();
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath()));
    } else if (chosen == add) {
        promptAddLinks();
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
    connect(dlg, &HosterPuzzleDialog::answered, this, [this, puzzle](const QString& text) {
        if (!m_ipc)
            return;
        IpcMessage req(IpcMsgType::AnswerHosterPuzzle);
        req.append(qint64(puzzle));
        req.append(text);
        m_ipc->sendRequest(std::move(req));
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void HosterPanel::onLinks(const IpcMessage& msg)
{
    const QCborArray links = msg.field(1).toArray();
    if (links.isEmpty())
        return;
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
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QStringList urls;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            urls << list->item(i)->data(Qt::UserRole).toString();
    addLinks(urls);
}

void HosterPanel::applyStatus(const QCborMap& st)
{
    const QCborMap h = st.value(QStringLiteral("hoster")).toMap();
    if (h.isEmpty())
        return;
    QString text;
    if (!h.value(QStringLiteral("enabled")).toBool())
        text = tr("Downloads from hosters are off (Options, ed3k, Hoster).");
    else if (const QCborMap r = h.value(QStringLiteral("resolver")).toMap(); !r.value(QStringLiteral("available")).toBool())
        text = tr("Resolver not ready: %1").arg(r.value(QStringLiteral("reason")).toString());
    else
        text = r.value(QStringLiteral("description")).toString();
    m_statusLabel->setText(text);
}

QList<qint64> HosterPanel::selectedIds() const
{
    QList<qint64> out;
    for (QTreeWidgetItem* row : m_tree->selectedItems())
        out << row->data(ColName, Qt::UserRole + 1).toLongLong();
    return out;
}

} // namespace eMule

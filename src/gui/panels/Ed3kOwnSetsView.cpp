#include "pch.h"
/// @file Ed3kOwnSetsView.cpp
/// @brief "My ed3k Sets" — implementation.

#include "panels/Ed3kOwnSetsView.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/SortableItems.h"
#include "dialogs/Ed3kSellDialogs.h"
#include "utils/AsyncMessageBox.h"
#include "utils/MenuUtils.h"
#include "utils/StringUtils.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QApplication>
#include <QCborMap>
#include <QClipboard>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

enum Col { ColTitle, ColFiles, ColSize, ColVersion, ColKind, ColSale };
constexpr int kIdRole = Qt::UserRole;

QString kindText(qint64 kind)
{
    switch (kind) {
    case 2:
        return Ed3kOwnSetsView::tr("Hidden");
    case 3:
        return Ed3kOwnSetsView::tr("Paid");
    default:
        return Ed3kOwnSetsView::tr("Open");
    }
}

} // namespace

Ed3kOwnSetsView::Ed3kOwnSetsView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* list = new ListTreeWidget;
    m_list = list;
    m_list->setHeaderLabels({tr("Title"), tr("Files"), tr("Size"), tr("Version"), tr("Kind"), tr("Sale")});
    m_list->setRootIsDecorated(false);
    m_list->setAlternatingRowColors(true);
    m_list->setSortingEnabled(true);
    m_list->sortByColumn(ColTitle, Qt::AscendingOrder);
    list->bindColumns(QStringLiteral("ed3kOwnSets"), {260, 60, 90, 90, 80, 110});
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, &Ed3kOwnSetsView::onContextMenu);
    layout->addWidget(m_list);
}

void Ed3kOwnSetsView::setIpcClient(IpcClient* ipc)
{
    if (m_ipc)
        disconnect(m_ipc, nullptr, this, nullptr);
    m_ipc = ipc;
    if (!m_ipc)
        return;
    connect(m_ipc, &IpcClient::ed3kOwnSetsChanged, this, [this](const IpcMessage& msg) {
        showRows(msg.fieldMap(0).value(QStringLiteral("sets")).toArray());
    });
    connect(m_ipc, &IpcClient::connected, this, &Ed3kOwnSetsView::refresh);
}

void Ed3kOwnSetsView::refresh()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    m_ipc->requestEd3kOwnSets([self = QPointer(this)](const IpcMessage& r) {
        if (self && r.fieldBool(0))
            self->showRows(r.fieldMap(1).value(QStringLiteral("sets")).toArray());
    });
}

void Ed3kOwnSetsView::choosePublishNewVersion(QWidget* parent)
{
    QStringList titles;
    QStringList ids;
    for (const auto& v : m_rows) {
        const QCborMap m = v.toMap();
        // a paid set not sold yet has no version to follow
        if (m.value(QStringLiteral("kind")).toInteger() == 3 && m.value(QStringLiteral("scope")).toString().isEmpty())
            continue;
        titles << m.value(QStringLiteral("title")).toString();
        ids << m.value(QStringLiteral("id")).toString();
    }
    if (ids.isEmpty()) {
        AsyncMessageBox::information(parent, tr("Publish New Version"), tr("You have no ed3k sets yet. Create one first."));
        return;
    }
    bool ok = false;
    const QString title = QInputDialog::getItem(parent, tr("Publish New Version"), tr("ed3k set:"), titles, 0, false, &ok);
    if (ok)
        control(1, ids.value(titles.indexOf(title)));
}

void Ed3kOwnSetsView::addSetActions(QMenu* menu, const QString& id)
{
    const QCborMap row = rowOf(id);
    const bool have = !row.isEmpty();
    const bool paid = row.value(QStringLiteral("kind")).toInteger() == 3;
    const bool sold = !row.value(QStringLiteral("scope")).toString().isEmpty();
    const bool stopped = row.value(QStringLiteral("stopped")).toBool();
    const bool published = row.value(QStringLiteral("published")).toBool();

    menu->addAction(tr("Publish New Version"), this, [this, id] { control(1, id); })->setEnabled(have && (!paid || sold));
    menu->addAction(menuIcon("eD2kLink.ico"), tr("Copy Link"), this, [this, id] { copyLink(id); })->setEnabled(have && (!paid || sold));
    menu->addAction(tr("Rename..."), this, [this, id] { rename(id); })->setEnabled(have);
    menu->addSeparator();
    // a hidden set is not sold (paid hidden sets are not offered)
    const bool sellable = have && row.value(QStringLiteral("kind")).toInteger() != 2;
    menu->addAction(sold ? tr("Change Terms...") : tr("Sell..."), this, [this, id] { sell(id); })->setEnabled(sellable);
    menu->addAction(stopped ? tr("Resume Selling") : tr("Stop Selling"), this, [this, id] { stopOrResume(id); })->setEnabled(sold);
    menu->addSeparator();
    menu->addAction(menuIcon("Delete.ico"), tr("Unpublish..."), this, [this, id] { unpublish(id); })->setEnabled(have && published);
}

void Ed3kOwnSetsView::showRows(const QCborArray& sets)
{
    m_rows = sets;
    const QString current = m_list->currentItem() ? m_list->currentItem()->data(ColTitle, kIdRole).toString() : QString();
    m_list->setSortingEnabled(false);
    m_list->clear();
    for (const auto& v : sets) {
        const QCborMap m = v.toMap();
        auto* item = new SortableTreeItem;
        const QString id = m.value(QStringLiteral("id")).toString();
        item->setData(ColTitle, kIdRole, id);
        item->setText(ColTitle, m.value(QStringLiteral("title")).toString());
        const QString error = m.value(QStringLiteral("error")).toString();
        if (!error.isEmpty())
            item->setToolTip(ColTitle, error);
        const qint64 files = m.value(QStringLiteral("files")).toInteger();
        item->setText(ColFiles, QString::number(files));
        item->setData(ColFiles, SortRole, files);
        item->setTextAlignment(ColFiles, Qt::AlignRight | Qt::AlignVCenter);
        const qint64 size = m.value(QStringLiteral("size")).toInteger();
        item->setText(ColSize, formatByteSize(size));
        item->setData(ColSize, SortRole, size);
        item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
        const qint64 seq = m.value(QStringLiteral("seq")).toInteger();
        QString version = seq > 0 ? QString::number(seq) : tr("none");
        if (m.value(QStringLiteral("building")).toBool())
            version = tr("publishing...");
        else if (seq > 0 && !m.value(QStringLiteral("published")).toBool())
            version = tr("%1 (unpublished)").arg(seq);
        else if (!error.isEmpty())
            version = tr("%1 (failed)").arg(seq);
        item->setText(ColVersion, version);
        item->setData(ColVersion, SortRole, seq);
        const qint64 kind = m.value(QStringLiteral("kind")).toInteger();
        item->setText(ColKind, kindText(kind));
        if (kind == 3) {
            const bool hasScope = !m.value(QStringLiteral("scope")).toString().isEmpty();
            item->setText(ColSale, !hasScope                                     ? tr("Not sold")
                                   : m.value(QStringLiteral("stopped")).toBool() ? tr("Stopped")
                                                                                 : tr("Sold"));
        }
        m_list->addTopLevelItem(item);
        if (id == current)
            m_list->setCurrentItem(item);
    }
    m_list->setSortingEnabled(true);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kOwnSetsView::onContextMenu(const QPoint& pos)
{
    const QTreeWidgetItem* item = m_list->itemAt(pos);
    const QString id = item ? item->data(ColTitle, kIdRole).toString() : QString();
    QMenu menu(this);
    addSetActions(&menu, id);
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

QCborMap Ed3kOwnSetsView::rowOf(const QString& id) const
{
    if (id.isEmpty())
        return {};
    for (const auto& v : m_rows)
        if (v.toMap().value(QStringLiteral("id")).toString() == id)
            return v.toMap();
    return {};
}

void Ed3kOwnSetsView::control(int action, const QString& id, const QString& title)
{
    if (!m_ipc || id.isEmpty())
        return;
    m_ipc->controlEd3kOwnSet(action, id, title, [self = QPointer(this), action](const IpcMessage& r) {
        if (self && !r.fieldBool(0))
            self->showError(action == 2 ? tr("The set was not unpublished.") : tr("No new version was published."), r);
    });
}

void Ed3kOwnSetsView::copyLink(const QString& id)
{
    if (!m_ipc)
        return;
    // a hidden set's link is a key: in the clipboard only, never logged
    m_ipc->requestEd3kOwnSetLink(id, [self = QPointer(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (!r.fieldBool(0)) {
            self->showError(tr("The link is not available."), r);
            return;
        }
        QApplication::clipboard()->setText(r.fieldMap(1).value(QStringLiteral("link")).toString());
        if (r.fieldMap(1).value(QStringLiteral("hidden")).toBool())
            AsyncMessageBox::information(self, tr("Copy Link"),
                                         tr("The link of the hidden set was copied. It is a key: whoever has it reads the "
                                            "set. Give it only to the people who should."));
    });
}

void Ed3kOwnSetsView::rename(const QString& id)
{
    const QCborMap row = rowOf(id);
    bool ok = false;
    const QString title = QInputDialog::getText(this, tr("Rename ed3k Set"), tr("Title:"), QLineEdit::Normal,
                                                row.value(QStringLiteral("title")).toString(), &ok)
                              .trimmed();
    if (ok && !title.isEmpty())
        control(3, id, title);
}

void Ed3kOwnSetsView::sell(const QString& id)
{
    if (!m_ipc)
        return;
    const QCborMap row = rowOf(id);
    const QString title = row.value(QStringLiteral("title")).toString();
    const QString scope = row.value(QStringLiteral("scope")).toString();
    // the defaults of a new offer (or the sale's terms), then the terms, then SellEd3k kind 2
    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetEd3kSelling), [self = QPointer(this), id, title, scope](const IpcMessage& r) {
        if (!self || !self->m_ipc)
            return;
        QCborMap terms = r.fieldBool(0) ? r.fieldMap(1).value(QStringLiteral("defaults")).toMap() : QCborMap{};
        if (!scope.isEmpty())
            for (const auto& v : r.fieldMap(1).value(QStringLiteral("sales")).toArray())
                if (v.toMap().value(QStringLiteral("scope")).toString() == scope)
                    terms = v.toMap();
        const auto chosen = Ed3kSellTermsDialog::ask(self, scope.isEmpty() ? Ed3kSellTermsDialog::Mode::NewSet : Ed3kSellTermsDialog::Mode::Change,
                                                     terms, tr("Sell \"%1\"").arg(title));
        if (!chosen)
            return;
        IpcMessage req(IpcMsgType::SellEd3k);
        req.append(qint64(2));
        req.append(id);
        req.append(title);
        req.append(*chosen);
        self->m_ipc->sendRequest(std::move(req), [self](const IpcMessage& a) {
            if (!self)
                return;
            if (!a.fieldBool(0)) {
                self->showError(tr("The set could not be sold."), a);
                return;
            }
            self->refresh();
        });
    });
}

void Ed3kOwnSetsView::stopOrResume(const QString& id)
{
    const QCborMap row = rowOf(id);
    const QString scope = row.value(QStringLiteral("scope")).toString();
    if (!m_ipc || scope.isEmpty())
        return;
    const bool stopped = row.value(QStringLiteral("stopped")).toBool();
    if (!stopped
        && QMessageBox::question(this, tr("Stop Selling"),
                                 tr("Stop selling \"%1\"?\n\nBuyers see it is no longer sold; passes held still work until they "
                                    "end. The files stay sealed.")
                                     .arg(row.value(QStringLiteral("title")).toString()))
               != QMessageBox::Yes)
        return;
    // §3 (#15): Ed3kSellControl 4 stop (an offer without price and shop), 5 sell again
    IpcMessage req(IpcMsgType::Ed3kSellControl);
    req.append(qint64(stopped ? 5 : 4));
    req.append(scope);
    req.append(QCborMap{});
    m_ipc->sendRequest(std::move(req), [self = QPointer(this), stopped](const IpcMessage& a) {
        if (self && !a.fieldBool(0))
            self->showError(stopped ? tr("It could not be sold again.") : tr("The sale could not be stopped."), a);
    });
}

void Ed3kOwnSetsView::unpublish(const QString& id)
{
    const QCborMap row = rowOf(id);
    if (QMessageBox::question(this, tr("Unpublish"),
                              tr("Stop publishing \"%1\"?\n\nIts files are no longer shared here; what the network holds of it "
                                 "runs out. \"Publish New Version\" brings it back.")
                                  .arg(row.value(QStringLiteral("title")).toString()),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    control(2, id);
}

void Ed3kOwnSetsView::showError(const QString& what, const IpcMessage& reply)
{
    const QString error = reply.fieldMap(1).value(QStringLiteral("error")).toString();
    AsyncMessageBox::warning(this, tr("My ed3k Sets"), error.isEmpty() ? what : what + QLatin1Char('\n') + error);
}

} // namespace eMule

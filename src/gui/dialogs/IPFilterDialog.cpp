#include "pch.h"
/// @file IPFilterDialog.cpp
/// @brief Tools > IP Filter... — implementation (MFC srchybrid/IPFilterDlg.cpp).

#include "dialogs/IPFilterDialog.h"

#include "app/IpcClient.h"
#include "dialogs/FindInListDialog.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "utils/IpcFeedback.h"
#include "utils/Clipboard.h"

#include <QApplication>
#include <QCborMap>
#include <QPointer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableView>
#include <QVBoxLayout>
#include <QtEndian>

#include <cstring>

namespace eMule {

// ---------------------------------------------------------------------------
// IpFilterModel
// ---------------------------------------------------------------------------

QVariant IpFilterModel::data(const QModelIndex& index, int role) const
{
    const IpFilterRow* r = rowAt(index.row());
    if (!r)
        return {};
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColStart: return r->startText;
        case ColEnd:   return r->endText;
        case ColLevel: return r->level;
        case ColHits:  return r->hits;
        case ColDesc:  return r->desc;
        default:       return {};
        }
    }
    if (role == Qt::TextAlignmentRole && (index.column() == ColLevel || index.column() == ColHits))
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

QVariant IpFilterModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColStart: return tr("Start");
    case ColEnd:   return tr("End");
    case ColLevel: return tr("Level");
    case ColHits:  return tr("Hits");
    case ColDesc:  return tr("Description");
    default:       return {};
    }
}

void IpFilterModel::sort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    // v4 before v6; bounds compare numerically
    const auto less = [column](const IpFilterRow& a, const IpFilterRow& b) {
        switch (column) {
        case ColStart:
            if (a.v6 != b.v6) return !a.v6;
            return a.v6 ? a.start6 < b.start6 : a.start4 < b.start4;
        case ColEnd:
            if (a.v6 != b.v6) return !a.v6;
            return a.v6 ? a.end6 < b.end6 : a.end4 < b.end4;
        case ColLevel: return a.level < b.level;
        case ColHits:  return a.hits < b.hits;
        case ColDesc:  return a.desc.compare(b.desc, Qt::CaseInsensitive) < 0;
        default:       return false;
        }
    };
    emit layoutAboutToBeChanged();
    if (order == Qt::AscendingOrder)
        std::stable_sort(m_rows.begin(), m_rows.end(), less);
    else
        std::stable_sort(m_rows.begin(), m_rows.end(),
                         [&](const IpFilterRow& a, const IpFilterRow& b) { return less(b, a); });
    emit layoutChanged();
}

std::vector<IpFilterRow> IpFilterModel::parse(const QByteArray& text)
{
    std::vector<IpFilterRow> rows;
    rows.reserve(static_cast<size_t>(text.count('\n')));
    const QByteArrayView all(text);
    qsizetype pos = 0;
    while (pos < all.size()) {
        qsizetype eol = all.indexOf('\n', pos);
        if (eol < 0)
            eol = all.size();
        const QByteArrayView line = all.sliced(pos, eol - pos);
        pos = eol + 1;
        // 5 tab-separated fields, then the description (rest of line)
        std::array<QByteArrayView, 6> f;
        qsizetype from = 0;
        int n = 0;
        for (; n < 5; ++n) {
            const qsizetype tab = line.indexOf('\t', from);
            if (tab < 0)
                break;
            f[static_cast<size_t>(n)] = line.sliced(from, tab - from);
            from = tab + 1;
        }
        if (n < 5)
            continue;
        f[5] = line.sliced(from);
        IpFilterRow r;
        r.v6 = f[0] == "6";
        if (r.v6) {
            const QByteArray s = QByteArray::fromHex(f[1].toByteArray());
            const QByteArray e = QByteArray::fromHex(f[2].toByteArray());
            if (s.size() != 16 || e.size() != 16)
                continue;
            std::memcpy(r.start6.data(), s.constData(), 16);
            std::memcpy(r.end6.data(), e.constData(), 16);
            r.startText = QHostAddress(r.start6.data()).toString();
            r.endText = QHostAddress(r.end6.data()).toString();
        } else {
            r.start4 = static_cast<uint32>(f[1].toULongLong());
            r.end4 = static_cast<uint32>(f[2].toULongLong());
            r.startText = QHostAddress(r.start4).toString();
            r.endText = QHostAddress(r.end4).toString();
        }
        r.level = static_cast<uint32>(f[3].toULong());
        r.hits = static_cast<uint32>(f[4].toULong());
        r.desc = QString::fromUtf8(f[5]);
        rows.push_back(std::move(r));
    }
    return rows;
}

void IpFilterModel::removeRowsAt(const std::vector<int>& rows)
{
    // Back to front, merging runs, so indexes stay valid
    for (auto it = rows.rbegin(); it != rows.rend();) {
        const int last = *it;
        int first = last;
        ++it;
        while (it != rows.rend() && *it == first - 1) {
            first = *it;
            ++it;
        }
        beginRemoveRows({}, first, last);
        m_rows.erase(m_rows.begin() + first, m_rows.begin() + last + 1);
        endRemoveRows();
    }
}

quint64 IpFilterModel::totalIPs() const
{
    quint64 total = 0;
    for (const auto& r : m_rows)
        if (!r.v6)
            total += static_cast<quint64>(r.end4) - r.start4 + 1;
    return total;
}

// ---------------------------------------------------------------------------
// IPFilterDialog
// ---------------------------------------------------------------------------

IPFilterDialog::IPFilterDialog(IpcClient* ipc, QWidget* parent)
    : RememberedSizeDialog(parent)
    , m_ipc(ipc)
{
    setWindowTitle(tr("IP Filter"));
    resize(720, 360);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("IP filters:"), this));

    m_model = new IpFilterModel(this);
    m_view = new ListTableView(this);
    m_view->setModel(m_model);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(IpFilterModel::ColStart, Qt::AscendingOrder);
    m_view->setShowGrid(false);
    m_view->setWordWrap(false);
    m_view->verticalHeader()->hide();
    m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 4);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->horizontalHeader()->setHighlightSections(false);
    m_view->bindColumns(QStringLiteral("ipFilter"), {130, 130, 50, 60});
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, &IPFilterDialog::showContextMenu);
    layout->addWidget(m_view, 1);

    auto* bottom = new QGridLayout;
    bottom->addWidget(new QLabel(tr("Total IP filters:"), this), 0, 0);
    m_totalFilters = new QLabel(this);
    bottom->addWidget(m_totalFilters, 0, 1);
    bottom->addWidget(new QLabel(tr("Total IPs:"), this), 1, 0);
    m_totalIps = new QLabel(this);
    bottom->addWidget(m_totalIps, 1, 1);
    bottom->setColumnStretch(2, 1);

    auto* buttons = new QHBoxLayout;
    m_deleteBtn = new QPushButton(tr("&Delete"), this);
    m_appendBtn = new QPushButton(tr("&Append..."), this);
    m_saveBtn = new QPushButton(tr("&Save"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    buttons->addWidget(m_deleteBtn);
    buttons->addWidget(m_appendBtn);
    buttons->addWidget(m_saveBtn);
    buttons->addWidget(closeBtn);
    bottom->addLayout(buttons, 0, 3, 2, 1, Qt::AlignBottom);
    layout->addLayout(bottom);

    connect(m_deleteBtn, &QPushButton::clicked, this, &IPFilterDialog::deleteSelected);
    connect(m_appendBtn, &QPushButton::clicked, this, &IPFilterDialog::appendFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &IPFilterDialog::save);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    // Del / Ctrl+C / Ctrl+F / F3 on the list, as MFC OnLvnKeyDownIPFilter
    auto shortcut = [this](const QKeySequence& key, auto slot) {
        auto* sc = new QShortcut(key, m_view, nullptr, nullptr, Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, this, slot);
    };
    shortcut(QKeySequence::Delete, &IPFilterDialog::deleteSelected);
    shortcut(QKeySequence::Copy, &IPFilterDialog::copySelected);
    shortcut(QKeySequence::Find, [this] { showFindInListDialog(this, m_view); });
    shortcut(QKeySequence::FindNext, [this] { findNextInList(this, m_view); });
    shortcut(QKeySequence::FindPrevious, [this] { findNextInList(this, m_view, true); });

    updateTotals();
    reload();
}

IPFilterDialog::~IPFilterDialog()
{
    // Closed while a request runs: don't leave the wait cursor behind
    if (m_busyCursor)
        QApplication::restoreOverrideCursor();
}

void IPFilterDialog::reload()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        setBusy(false);
        m_deleteBtn->setEnabled(false);
        m_appendBtn->setEnabled(false);
        m_saveBtn->setEnabled(false);
        return;
    }
    setBusy(true);
    m_ipc->sendRequest(Ipc::IpcMessage(Ipc::IpcMsgType::GetIPFilter),
        [self = QPointer<IPFilterDialog>(this)](const Ipc::IpcMessage& resp) {
            if (!self)
                return;
            self->setBusy(false);
            if (!resp.isValid() || resp.type() != Ipc::IpcMsgType::Result || !resp.fieldBool(0))
                return;
            const QByteArray text = qUncompress(resp.fieldMap(1).value(QStringLiteral("list")).toByteArray());
            self->m_model->setRows(IpFilterModel::parse(text));
            const QHeaderView* h = self->m_view->horizontalHeader();
            self->m_model->sort(h->sortIndicatorSection(), h->sortIndicatorOrder());
            self->updateTotals();
        });
}

void IPFilterDialog::deleteSelected()
{
    const std::vector<int> rows = selectedRows();
    if (rows.empty() || !m_ipc || !m_ipc->isConnected())
        return;
    if (QMessageBox::question(this, tr("IP Filter"),
                              tr("Do you really want to delete the selected IP filters?"),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    const bool all = static_cast<int>(rows.size()) == m_model->count();
    QByteArray v4;
    QByteArray v6;
    if (!all) {
        for (int row : rows) {
            const IpFilterRow* r = m_model->rowAt(row);
            if (!r)
                continue;
            if (r->v6) {
                v6.append(reinterpret_cast<const char*>(r->start6.data()), 16);
            } else {
                char be[4];
                qToBigEndian<quint32>(r->start4, be);
                v4.append(be, 4);
            }
        }
    }
    Ipc::IpcMessage req(Ipc::IpcMsgType::RemoveIPFilters);
    req.append(all);
    req.append(QCborValue(v4));
    req.append(QCborValue(v6));
    setBusy(true);
    m_ipc->sendRequest(std::move(req),
        [self = QPointer<IPFilterDialog>(this), rows](const Ipc::IpcMessage& resp) {
            if (!self)
                return;
            self->setBusy(false);
            if (!IpcFeedback::checkOrWarn(resp, self, self->windowTitle()))
                return;
            const int first = rows.front();
            self->m_model->removeRowsAt(rows);
            // Select the row that took the first deleted one's place
            const int next = std::min(first, self->m_model->count() - 1);
            if (next >= 0)
                self->m_view->selectRow(next);
            self->updateTotals();
        });
}

void IPFilterDialog::appendFile()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Append IP Filter"), {},
        tr("All IP Filter Files (*ipfilter.dat *ip.prefix *.p2b *.p2p *.p2p.txt);;"
           "eMule IP Filter Files (*ipfilter.dat *ip.prefix);;"
           "Peer Guardian Files (*.p2b *.p2p *.p2p.txt);;"
           "Text Files (*.txt);;All Files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    // The file goes to the daemon in one message, so it must fit one
    if (file.size() > Ipc::MaxPayloadSize - 4096) {
        QMessageBox::warning(this, windowTitle(), tr("The file is too large."));
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), file.errorString());
        return;
    }
    Ipc::IpcMessage req(Ipc::IpcMsgType::AppendIPFilter);
    req.append(QFileInfo(path).fileName());
    req.append(QCborValue(file.readAll()));
    setBusy(true);
    m_ipc->sendRequest(std::move(req), [self = QPointer<IPFilterDialog>(this)](const Ipc::IpcMessage& resp) {
        if (!self)
            return;
        self->setBusy(false);
        if (IpcFeedback::checkOrWarn(resp, self, self->windowTitle()))
            self->reload();
    });
}

void IPFilterDialog::save()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    setBusy(true);
    m_ipc->sendRequest(Ipc::IpcMessage(Ipc::IpcMsgType::SaveIPFilter),
        [self = QPointer<IPFilterDialog>(this)](const Ipc::IpcMessage& resp) {
            if (!self)
                return;
            self->setBusy(false);
            IpcFeedback::checkOrWarn(resp, self, self->windowTitle(),
                                     tr("Failed to save the IP filter file."));
        });
}

void IPFilterDialog::copySelected()
{
    // MFC OnCopyIPFilter line format
    QStringList lines;
    for (int row : selectedRows()) {
        const IpFilterRow* r = m_model->rowAt(row);
        if (!r)
            continue;
        lines << QStringLiteral("%1 - %2  Hits=%3  %4")
                     .arg(r->startText, -15).arg(r->endText, -15)
                     .arg(QString::number(r->hits), -5).arg(r->desc);
    }
    if (lines.isEmpty())
        return;
    QString text = lines.join(QStringLiteral("\r\n"));
    if (lines.size() > 1)
        text += QStringLiteral("\r\n");
    Clipboard::setText(text);
}

void IPFilterDialog::showContextMenu(const QPoint& pos)
{
    const bool any = !selectedRows().empty();
    QMenu menu(this);
    menu.addAction(tr("Copy"), this, &IPFilterDialog::copySelected)->setEnabled(any);
    menu.addAction(tr("Remove"), this, &IPFilterDialog::deleteSelected)->setEnabled(any && m_deleteBtn->isEnabled());
    menu.addSeparator();
    menu.addAction(tr("Select All"), m_view, &QAbstractItemView::selectAll);
    menu.addSeparator();
    menu.addAction(tr("Find..."), this, [this] { showFindInListDialog(this, m_view); });
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void IPFilterDialog::updateTotals()
{
    const QLocale loc;
    m_totalFilters->setText(loc.toString(m_model->count()));
    m_totalIps->setText(loc.toString(m_model->totalIPs()));
}

std::vector<int> IPFilterDialog::selectedRows() const
{
    std::vector<int> rows;
    for (const QModelIndex& idx : m_view->selectionModel()->selectedRows())
        rows.push_back(idx.row());
    std::ranges::sort(rows);
    return rows;
}

void IPFilterDialog::setBusy(bool busy)
{
    m_deleteBtn->setEnabled(!busy);
    m_appendBtn->setEnabled(!busy);
    m_saveBtn->setEnabled(!busy);
    if (busy == m_busyCursor)
        return;
    m_busyCursor = busy;
    if (busy)
        QApplication::setOverrideCursor(Qt::WaitCursor);
    else
        QApplication::restoreOverrideCursor();
}

} // namespace eMule

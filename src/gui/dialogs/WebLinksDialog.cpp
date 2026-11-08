#include "pch.h"
/// @file WebLinksDialog.cpp
/// @brief The links behind an eNode web search row.

#include "dialogs/WebLinksDialog.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/FileCheckButtons.h"
#include "app/UiState.h"
#include "prefs/Preferences.h"
#include "utils/Clipboard.h"
#include "utils/DialogSizing.h"
#include "utils/HosterLinkStatus.h"
#include "utils/Ed2kLinkImporter.h"
#include "utils/MenuUtils.h"
#include "utils/StatusBarNotifier.h"
#include "utils/StringUtils.h"
#include "app/SkinIcons.h"

#include <QApplication>
#include <QCborArray>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QMap>
#include <QMenu>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

namespace eMule {

namespace {

enum Column { ColFile, ColName, ColSize, ColHoster, ColStatus, ColAccount, ColLink };

using gui::drawnIcon;

/// Ed3kHosterJoin::LinkStatus (HosterLinkStatus.h)
enum LinkStatus { StatusChecking = gui::LinkChecking, StatusOnline = gui::LinkOnline, StatusOffline = gui::LinkOffline,
                  StatusUnknown = gui::LinkUnknown };

/// Opening more than this at once asks first: each is a browser tab.
constexpr int kOpenWithoutAsking = 5;

/// uistate.yml key of the list's column layout.
const QString kStateKey = QStringLiteral("webLinks");

/// A golden star, for a premium account.
QIcon premiumIcon()
{
    static const QIcon icon = drawnIcon([](QPainter& p) {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double r = i % 2 ? 3.0 : 7.0;
            const double a = (-90.0 + i * 36.0) * std::numbers::pi / 180.0;
            star << QPointF(8.0 + r * std::cos(a), 8.5 + r * std::sin(a));
        }
        p.setPen(QPen(QColor(0xB0, 0x80, 0x00), 0.8));
        p.setBrush(QColor(0xFF, 0xC8, 0x1E));
        p.drawPolygon(star);
    });
    return icon;
}

} // namespace

WebLinksDialog::WebLinksDialog(const QString& rowName, const QCborMap& links, const QByteArray& content,
                               const QString& fileName, IpcClient* ipc, QWidget* parent)
    : RememberedSizeDialog(parent)
    , m_content(content)
    , m_fileName(fileName)
    , m_rowName(rowName)
    , m_ipc(ipc)
{
    const QString title = links.value(QStringLiteral("title")).toString();
    setWindowTitle(tr("Links: %1").arg(title.isEmpty() ? rowName : title));
    setWindowIcon(skinIcon(QStringLiteral(":/icons/Web.ico")));

    auto* layout = new QVBoxLayout(this);
    if (links.value(QStringLiteral("protected")).toBool()) {
        auto* note = new QLabel(tr("The page says these files are password protected."), this);
        note->setWordWrap(true);
        layout->addWidget(note);
    }

    // as a torrent's or an NZB's Files tab: boxes pick what Download takes
    auto* hint = new QLabel(tr("Unchecked links are not downloaded. Links with the same file number "
                               "are mirrors: the next one is tried when one fails."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_list = new ListTreeWidget(this);
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setHeaderLabels({tr("File"), tr("Name"), tr("Size"), tr("Hoster"), tr("Status"), tr("Account"), tr("Link")});
    for (const QCborValue& v : links.value(QStringLiteral("links")).toArray()) {
        const QCborMap l = v.toMap();
        const qint64 size = l.value(QStringLiteral("size")).toInteger();
        auto* item = new QTreeWidgetItem(m_list);
        item->setText(ColFile, QString::number(l.value(QStringLiteral("file")).toInteger() + 1));
        item->setText(ColName, l.value(QStringLiteral("name")).toString());
        item->setText(ColSize, size > 0 ? formatByteSize(size) : QString());
        item->setText(ColHoster, l.value(QStringLiteral("hoster")).toString());
        item->setText(ColLink, l.value(QStringLiteral("url")).toString());
        item->setData(ColFile, Qt::UserRole, l.value(QStringLiteral("file")).toInteger());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(ColFile, Qt::Checked);
        item->setTextAlignment(ColFile, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
    }
    for (int c = ColFile; c < ColLink; ++c)
        m_list->resizeColumnToContents(c);
    // saved widths, order and hidden columns win over the fitted defaults
    m_freshLayout = m_fitOnShow = !theUiState.hasHeaderState(kStateKey);
    m_list->bindColumns(kStateKey);
    m_wantStatus = !m_list->isColumnHidden(ColStatus);
    m_wantAccount = !m_list->isColumnHidden(ColAccount);
    // no data yet: startChecks() / setStatus() bring them back
    lockColumn(ColStatus);
    lockColumn(ColAccount);
    layout->addWidget(FileCheckButtons::create(this, [this](FileCheckButtons::Bulk mode) {
        FileCheckButtons::applyToLeaves(m_list, ColFile, mode);
    }));
    layout->addWidget(m_list, 1);

    if (hasOnionLink()) {
        auto* note = new QLabel(tr("Links to .onion addresses are Tor hidden services. "
                                   "Copy them into Tor Browser: other browsers cannot open them."), this);
        note->setWordWrap(true);
        note->setObjectName(QStringLiteral("onionNote"));
        layout->addWidget(note);
    }
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [](QTreeWidgetItem* item) {
        QDesktopServices::openUrl(QUrl(item->text(ColLink), QUrl::StrictMode));
    });
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, &WebLinksDialog::showContextMenu);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // as Usenet and torrent rows: Download while the Hoster tab takes links, saving beside it
    if (Ed2kLinkImporter::hosterReady()) {
        m_downloadBtn = buttons->addButton(tr("Download"), QDialogButtonBox::AcceptRole);
        m_downloadBtn->setIcon(skinIcon(QStringLiteral(":/icons/Download.ico")));
        m_downloadBtn->setDefault(true);
        connect(m_downloadBtn, &QPushButton::clicked, this, &WebLinksDialog::downloadLinks);
        updateDownloadLabel();
    }
    auto* copyBtn = buttons->addButton(tr("Copy Links"), QDialogButtonBox::ActionRole);
    copyBtn->setIcon(skinIcon(QStringLiteral(":/icons/Copy.ico")));
    m_openBtn = buttons->addButton(tr("Open in Browser"), QDialogButtonBox::ActionRole);
    m_openBtn->setIcon(skinIcon(QStringLiteral(":/icons/Web.ico")));
    auto* saveBtn = buttons->addButton(tr("Save List..."), QDialogButtonBox::ActionRole);
    connect(copyBtn, &QPushButton::clicked, this, &WebLinksDialog::copyLinks);
    connect(m_openBtn, &QPushButton::clicked, this, &WebLinksDialog::openLinks);
    connect(saveBtn, &QPushButton::clicked, this, &WebLinksDialog::saveList);
    connect(m_list, &QTreeWidget::itemChanged, this, [this, copyBtn](QTreeWidgetItem*, int column) {
        if (column != ColFile)
            return;
        updateDownloadLabel();
        const bool any = !checkedItems().isEmpty();
        copyBtn->setEnabled(any);
        m_openBtn->setEnabled(any);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    DialogSizing::applySize(this, {}, QSize(720, 360), DialogSizing::Fit::Layout);
    startChecks();
}

WebLinksDialog::~WebLinksDialog()
{
    // Status/Account hidden for lack of data are not the user's choice: store their wish
    const std::set<int>& locked = m_list->lockedColumns();
    if (!locked.contains(ColStatus))
        m_wantStatus = !m_list->isColumnHidden(ColStatus);
    if (!locked.contains(ColAccount))
        m_wantAccount = !m_list->isColumnHidden(ColAccount);
    m_list->setColumnHidden(ColStatus, !m_wantStatus);
    m_list->setColumnHidden(ColAccount, !m_wantAccount);
    theUiState.captureHeaderState(m_list->header(), kStateKey);
}

QList<QStringList> WebLinksDialog::mirrorsByFile(const QCborArray& links)
{
    QMap<qint64, QStringList> byFile;
    for (const QCborValue& v : links) {
        const QCborMap l = v.toMap();
        if (const QString url = l.value(QStringLiteral("url")).toString(); !url.isEmpty())
            byFile[l.value(QStringLiteral("file")).toInteger()] << url;
    }
    return byFile.values();
}

void WebLinksDialog::showEvent(QShowEvent* event)
{
    RememberedSizeDialog::showEvent(event);
    // after the size restore: fit to the width the window opens at
    if (m_fitOnShow && !event->spontaneous()) {
        m_fitOnShow = false;
        if (layout())
            layout()->activate();
        fitNameColumn();
    }
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void WebLinksDialog::fitNameColumn()
{
    auto* hdr = m_list->header();
    // Status is in by now when the links get checked: startChecks() ran in the constructor
    int others = 0;
    for (const int c : {ColFile, ColSize, ColHoster, ColStatus, ColAccount})
        if (!m_list->isColumnHidden(c))
            others += hdr->sectionSize(c);
    const int minName = m_list->fontMetrics().averageCharWidth() * 12;
    const int room = m_list->viewport()->width() - others;
    const int fitted = std::min(hdr->sectionSize(ColName), std::max(room, minName));
    if (fitted != hdr->sectionSize(ColName))
        m_list->setColumnWidth(ColName, fitted);
}

void WebLinksDialog::unlockColumn(int column, bool wanted)
{
    std::set<int> locked = m_list->lockedColumns();
    if (!locked.erase(column))
        return;
    m_list->setLockedColumns(locked);
    if (wanted) {
        m_list->setColumnHidden(column, false);
        if (m_freshLayout)
            m_list->resizeColumnToContents(column);
    }
}

void WebLinksDialog::lockColumn(int column)
{
    std::set<int> locked = m_list->lockedColumns();
    // remember a choice made from the header menu while it was in there
    if (!locked.contains(column))
        (column == ColStatus ? m_wantStatus : m_wantAccount) = !m_list->isColumnHidden(column);
    locked.insert(column);
    m_list->setLockedColumns(locked);
    m_list->setColumnHidden(column, true);
}

bool WebLinksDialog::hasOnionLink() const
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        const QString host = QUrl(m_list->topLevelItem(i)->text(ColLink)).host();
        if (host.endsWith(QLatin1String(".onion"), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QList<QTreeWidgetItem*> WebLinksDialog::checkedItems() const
{
    QList<QTreeWidgetItem*> items;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (QTreeWidgetItem* item = m_list->topLevelItem(i); item->checkState(ColFile) == Qt::Checked)
            items << item;
    return items;
}

QStringList WebLinksDialog::chosenUrls() const
{
    QStringList urls;
    for (const QTreeWidgetItem* item : checkedItems())
        urls << item->text(ColLink);
    return urls;
}

QList<QStringList> WebLinksDialog::chosenDownloadFiles() const
{
    // list order: mirrors are tried in the page's order
    QCborArray links;
    for (const QTreeWidgetItem* item : checkedItems())
        links.append(QCborMap{{QStringLiteral("url"), item->text(ColLink)},
                              {QStringLiteral("file"), item->data(ColFile, Qt::UserRole).toLongLong()}});
    return mirrorsByFile(links);
}

void WebLinksDialog::downloadLinks()
{
    const QList<QStringList> files = chosenDownloadFiles();
    if (files.isEmpty())
        return;
    // the search row's name is the package
    if (!Ed2kLinkImporter::sendHosterLinks(files, parentWidget(), m_rowName)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not add the links of \"%1\": downloads from hosters are off.").arg(m_rowName));
        return;
    }
    StatusBarNotifier::post(tr("Added %n file(s) of \"%1\" to the Hoster tab.", nullptr, int(files.size()))
                                .arg(m_rowName));
    accept();
}

void WebLinksDialog::updateDownloadLabel()
{
    if (!m_downloadBtn)
        return;
    const auto n = checkedItems().size();
    const auto total = m_list->topLevelItemCount();
    if (n > 0 && n < total)
        m_downloadBtn->setText(tr("Download (%1)").arg(n));
    else if (n > 0 && total > 1)
        m_downloadBtn->setText(tr("Download (all)"));
    else
        m_downloadBtn->setText(tr("Download"));
    m_downloadBtn->setEnabled(n > 0);
}

void WebLinksDialog::showContextMenu(const QPoint& pos)
{
    // a right click on an unselected row picks it, as in the search lists
    if (QTreeWidgetItem* at = m_list->itemAt(pos); at && !at->isSelected()) {
        m_list->clearSelection();
        at->setSelected(true);
    }
    const QList<QTreeWidgetItem*> selected = m_list->selectedItems();
    const auto setChecked = [selected](bool on) {
        for (QTreeWidgetItem* item : selected)
            item->setCheckState(ColFile, on ? Qt::Checked : Qt::Unchecked);
    };
    QMenu menu(this);
    if (m_downloadBtn) {
        // as the search results' menu: Download is the bold default
        auto* download = menu.addAction(menuIcon("Download.ico"), tr("Download"), this, &WebLinksDialog::downloadLinks);
        download->setEnabled(!checkedItems().isEmpty());
        if (download->isEnabled())
            menu.setDefaultAction(download);
        menu.addSeparator();
    }
    menu.addAction(tr("Check Selected"), this, [setChecked] { setChecked(true); })->setEnabled(!selected.isEmpty());
    menu.addAction(tr("Uncheck Selected"), this, [setChecked] { setChecked(false); })->setEnabled(!selected.isEmpty());
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void WebLinksDialog::startChecks()
{
    if (!m_ipc || !m_ipc->isConnected() || !Ed2kLinkImporter::hosterReady() || !thePrefs.ed3kHoster().checkSearchLinks)
        return;
    QCborArray urls;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_list->topLevelItem(i);
        urls.append(item->text(ColLink));
        setStatus(item, StatusChecking, 0);
    }
    if (urls.isEmpty())
        return;
    unlockColumn(ColStatus, m_wantStatus);
    connect(m_ipc, &IpcClient::hosterLinkChecked, this, &WebLinksDialog::onLinkChecked);
    Ipc::IpcMessage msg(Ipc::IpcMsgType::CheckHosterLinks);
    msg.append(urls);
    QPointer<WebLinksDialog> self(this);
    m_ipc->sendRequest(std::move(msg), [self](const Ipc::IpcMessage& resp) {
        // the daemon will not check (switched off there): nothing to show
        if (self && resp.isValid() && !resp.fieldBool(0)) {
            self->lockColumn(ColStatus);
            self->lockColumn(ColAccount);
        }
    });
}

void WebLinksDialog::onLinkChecked(const Ipc::IpcMessage& msg)
{
    const QString url = msg.fieldString(0);
    const QCborMap r = msg.fieldMap(1);
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (QTreeWidgetItem* item = m_list->topLevelItem(i); item->text(ColLink) == url)
            setStatus(item, int(r.value(QStringLiteral("status")).toInteger()),
                      int(r.value(QStringLiteral("account")).toInteger()));
}

void WebLinksDialog::setStatus(QTreeWidgetItem* item, int status, int account)
{
    QString text;
    switch (status) {
    case StatusChecking: text = tr("Checking..."); break;
    case StatusOnline: text = tr("Online"); break;
    case StatusOffline: text = tr("Offline"); break;
    default: text = tr("Unknown"); break;
    }
    item->setText(ColStatus, text);
    item->setIcon(ColStatus, gui::linkStatusIcon(status));
    // the login Download would use there
    const bool usable = status != StatusChecking && status != StatusOffline;
    item->setText(ColAccount, !usable ? QString() : account == 2 ? tr("Premium account") : account == 1 ? tr("Account") : QString());
    item->setIcon(ColAccount, usable && account == 2 ? premiumIcon() : QIcon());
    if (usable && account > 0)
        unlockColumn(ColAccount, m_wantAccount);
    // a dead link: greyed, Download puts it last
    const QVariant fg = status == StatusOffline ? QVariant(palette().brush(QPalette::Disabled, QPalette::Text)) : QVariant();
    for (int c = ColFile; c <= ColLink; ++c)
        item->setData(c, Qt::ForegroundRole, fg);
    // a saved width is the user's
    if (m_freshLayout) {
        if (!m_list->isColumnHidden(ColStatus))
            m_list->resizeColumnToContents(ColStatus);
        if (!m_list->isColumnHidden(ColAccount))
            m_list->resizeColumnToContents(ColAccount);
    }
}

void WebLinksDialog::copyLinks()
{
    const QStringList urls = chosenUrls();
    eMule::Clipboard::setText(urls.join(u'\n'));
    StatusBarNotifier::post(tr("Copied %n link(s) to the clipboard.", nullptr, int(urls.size())));
}

void WebLinksDialog::openLinks()
{
    const QStringList urls = chosenUrls();
    if (urls.size() > kOpenWithoutAsking
        && QMessageBox::question(this, windowTitle(), tr("Open %n links in the browser?", nullptr, int(urls.size())),
                                 QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    // the daemon's parser let through http and https only
    for (const QString& url : urls)
        QDesktopServices::openUrl(QUrl(url, QUrl::StrictMode));
}

void WebLinksDialog::saveList()
{
    QString dir = theUiState.metaFileSaveDir();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Link List"), QDir(dir).filePath(m_fileName),
                                                      tr("Link lists (*.txt);;All files (*)"));
    if (path.isEmpty())
        return;
    theUiState.setMetaFileSaveDir(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(m_content) != m_content.size()) {
        QMessageBox::warning(this, windowTitle(), tr("Could not write %1: %2")
                                                      .arg(QDir::toNativeSeparators(path), file.errorString()));
        return;
    }
    StatusBarNotifier::post(tr("Saved %1").arg(QDir::toNativeSeparators(path)));
}

} // namespace eMule

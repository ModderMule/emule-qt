#include "pch.h"
/// @file WebLinksDialog.cpp
/// @brief The links behind an eNode web search row.

#include "dialogs/WebLinksDialog.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "prefs/Preferences.h"
#include "utils/Clipboard.h"
#include "utils/DialogSizing.h"
#include "utils/Ed2kLinkImporter.h"
#include "utils/StatusBarNotifier.h"
#include "utils/StringUtils.h"

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
#include <QMap>
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

/// Ed3kHosterJoin::LinkStatus, as PushHosterLinkCheck carries it.
enum LinkStatus { StatusChecking = 0, StatusOnline, StatusOffline, StatusUnknown };

/// Opening more than this at once asks first: each is a browser tab.
constexpr int kOpenWithoutAsking = 5;

/// 16 px icon, 1x and 2x, drawn by @p paint into a 16 x 16 box.
QIcon drawnIcon(const std::function<void(QPainter&)>& paint)
{
    QIcon icon;
    for (const int scale : {1, 2}) {
        QPixmap pm(16 * scale, 16 * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        paint(p);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

/// The status dot: green online, red offline, grey unknown; none while checking.
QIcon statusIcon(int status)
{
    QColor fill;
    switch (status) {
    case StatusOnline: fill = QColor(0x2E, 0xB8, 0x3C); break;
    case StatusOffline: fill = QColor(0xD9, 0x30, 0x25); break;
    case StatusUnknown: fill = QColor(0x9A, 0x9A, 0x9A); break;
    default: return {};
    }
    static QHash<int, QIcon> cache;
    if (const auto it = cache.constFind(status); it != cache.cend())
        return *it;
    return cache[status] = drawnIcon([fill](QPainter& p) {
        p.setPen(QPen(fill.darker(140), 1.0));
        p.setBrush(fill);
        p.drawEllipse(QRectF(3.5, 3.5, 9, 9));
    });
}

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
    : QDialog(parent)
    , m_content(content)
    , m_fileName(fileName)
    , m_rowName(rowName)
    , m_ipc(ipc)
{
    const QString title = links.value(QStringLiteral("title")).toString();
    setWindowTitle(tr("Links: %1").arg(title.isEmpty() ? rowName : title));
    setWindowIcon(QIcon(QStringLiteral(":/icons/Web.ico")));

    auto* layout = new QVBoxLayout(this);
    if (links.value(QStringLiteral("protected")).toBool()) {
        auto* note = new QLabel(tr("The page says these files are password protected."), this);
        note->setWordWrap(true);
        layout->addWidget(note);
    }

    m_list = new QTreeWidget(this);
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
        item->setTextAlignment(ColFile, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
    }
    m_list->setColumnHidden(ColStatus, true);
    m_list->setColumnHidden(ColAccount, true);
    for (int c = ColFile; c < ColLink; ++c)
        m_list->resizeColumnToContents(c);
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

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // as Usenet and torrent rows: Download while the Hoster tab takes links, saving beside it
    if (Ed2kLinkImporter::hosterReady()) {
        auto* downloadBtn = buttons->addButton(tr("Download"), QDialogButtonBox::AcceptRole);
        downloadBtn->setIcon(QIcon(QStringLiteral(":/icons/Download.ico")));
        downloadBtn->setDefault(true);
        connect(downloadBtn, &QPushButton::clicked, this, &WebLinksDialog::downloadLinks);
    }
    auto* copyBtn = buttons->addButton(tr("Copy Links"), QDialogButtonBox::ActionRole);
    copyBtn->setIcon(QIcon(QStringLiteral(":/icons/Copy.ico")));
    m_openBtn = buttons->addButton(tr("Open in Browser"), QDialogButtonBox::ActionRole);
    m_openBtn->setIcon(QIcon(QStringLiteral(":/icons/Web.ico")));
    auto* saveBtn = buttons->addButton(tr("Save List..."), QDialogButtonBox::ActionRole);
    connect(copyBtn, &QPushButton::clicked, this, &WebLinksDialog::copyLinks);
    connect(m_openBtn, &QPushButton::clicked, this, &WebLinksDialog::openLinks);
    connect(saveBtn, &QPushButton::clicked, this, &WebLinksDialog::saveList);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    DialogSizing::applySize(this, {}, QSize(720, 360), DialogSizing::Fit::Layout);
    startChecks();
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

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

bool WebLinksDialog::hasOnionLink() const
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        const QString host = QUrl(m_list->topLevelItem(i)->text(ColLink)).host();
        if (host.endsWith(QLatin1String(".onion"), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QStringList WebLinksDialog::chosenUrls() const
{
    QStringList urls;
    const auto selected = m_list->selectedItems();
    if (selected.isEmpty()) {
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            urls << m_list->topLevelItem(i)->text(ColLink);
    } else {
        for (const QTreeWidgetItem* item : selected)
            urls << item->text(ColLink);
    }
    return urls;
}

QList<QStringList> WebLinksDialog::chosenDownloadFiles() const
{
    QList<QTreeWidgetItem*> items = m_list->selectedItems();
    if (items.isEmpty())
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            items << m_list->topLevelItem(i);
    else   // list order, not click order: mirrors are tried in the page's order
        std::ranges::sort(items, {}, [this](QTreeWidgetItem* it) { return m_list->indexOfTopLevelItem(it); });
    QCborArray links;
    for (const QTreeWidgetItem* item : items)
        links.append(QCborMap{{QStringLiteral("url"), item->text(ColLink)},
                              {QStringLiteral("file"), item->data(ColFile, Qt::UserRole).toLongLong()}});
    return mirrorsByFile(links);
}

void WebLinksDialog::downloadLinks()
{
    const QList<QStringList> files = chosenDownloadFiles();
    if (files.isEmpty())
        return;
    if (!Ed2kLinkImporter::sendHosterLinks(files, parentWidget())) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not add the links of \"%1\": downloads from hosters are off.").arg(m_rowName));
        return;
    }
    StatusBarNotifier::post(tr("Added %n file(s) of \"%1\" to the Hoster tab.", nullptr, int(files.size()))
                                .arg(m_rowName));
    accept();
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
    m_list->setColumnHidden(ColStatus, false);
    m_list->resizeColumnToContents(ColStatus);
    connect(m_ipc, &IpcClient::hosterLinkChecked, this, &WebLinksDialog::onLinkChecked);
    Ipc::IpcMessage msg(Ipc::IpcMsgType::CheckHosterLinks);
    msg.append(urls);
    QPointer<WebLinksDialog> self(this);
    m_ipc->sendRequest(std::move(msg), [self](const Ipc::IpcMessage& resp) {
        // the daemon will not check (switched off there): nothing to show
        if (self && resp.isValid() && !resp.fieldBool(0)) {
            self->m_list->setColumnHidden(ColStatus, true);
            self->m_list->setColumnHidden(ColAccount, true);
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
    item->setIcon(ColStatus, statusIcon(status));
    // the login Download would use there
    const bool usable = status != StatusChecking && status != StatusOffline;
    item->setText(ColAccount, !usable ? QString() : account == 2 ? tr("Premium account") : account == 1 ? tr("Account") : QString());
    item->setIcon(ColAccount, usable && account == 2 ? premiumIcon() : QIcon());
    if (usable && account > 0 && m_list->isColumnHidden(ColAccount)) {
        m_list->setColumnHidden(ColAccount, false);
        m_list->resizeColumnToContents(ColAccount);
    }
    // a dead link: greyed, Download puts it last
    const QVariant fg = status == StatusOffline ? QVariant(palette().brush(QPalette::Disabled, QPalette::Text)) : QVariant();
    for (int c = ColFile; c <= ColLink; ++c)
        item->setData(c, Qt::ForegroundRole, fg);
    m_list->resizeColumnToContents(ColStatus);
    if (!m_list->isColumnHidden(ColAccount))
        m_list->resizeColumnToContents(ColAccount);
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

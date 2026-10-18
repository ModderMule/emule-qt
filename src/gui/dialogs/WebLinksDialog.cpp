#include "pch.h"
/// @file WebLinksDialog.cpp
/// @brief The links behind an eNode web search row.

#include "dialogs/WebLinksDialog.h"

#include "app/UiState.h"
#include "utils/DialogSizing.h"
#include "utils/StatusBarNotifier.h"
#include "utils/StringUtils.h"

#include <QApplication>
#include <QCborArray>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

namespace {

enum Column { ColFile, ColName, ColSize, ColHoster, ColLink };

/// Opening more than this at once asks first: each is a browser tab.
constexpr int kOpenWithoutAsking = 5;

} // namespace

WebLinksDialog::WebLinksDialog(const QString& rowName, const QCborMap& links, const QByteArray& content,
                               const QString& fileName, QWidget* parent)
    : QDialog(parent)
    , m_content(content)
    , m_fileName(fileName)
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
    m_list->setHeaderLabels({tr("File"), tr("Name"), tr("Size"), tr("Hoster"), tr("Link")});
    for (const QCborValue& v : links.value(QStringLiteral("links")).toArray()) {
        const QCborMap l = v.toMap();
        const qint64 size = l.value(QStringLiteral("size")).toInteger();
        auto* item = new QTreeWidgetItem(m_list);
        item->setText(ColFile, QString::number(l.value(QStringLiteral("file")).toInteger() + 1));
        item->setText(ColName, l.value(QStringLiteral("name")).toString());
        item->setText(ColSize, size > 0 ? formatByteSize(size) : QString());
        item->setText(ColHoster, l.value(QStringLiteral("hoster")).toString());
        item->setText(ColLink, l.value(QStringLiteral("url")).toString());
        item->setTextAlignment(ColFile, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
    }
    for (int c = ColFile; c < ColLink; ++c)
        m_list->resizeColumnToContents(c);
    layout->addWidget(m_list, 1);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [](QTreeWidgetItem* item) {
        QDesktopServices::openUrl(QUrl(item->text(ColLink), QUrl::StrictMode));
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
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
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

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

void WebLinksDialog::copyLinks()
{
    const QStringList urls = chosenUrls();
    QApplication::clipboard()->setText(urls.join(u'\n'));
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

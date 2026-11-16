#include "pch.h"
#include "controls/ArchiveContentsView.h"

#include "controls/FileCheckButtons.h"
#include "utils/StringUtils.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

namespace {

enum Role {
    RoleRoot = Qt::UserRole,   ///< entry: root hex
    RolePath,                  ///< entry: path below the download folder
    RoleSize,
    RoleKind,                  ///< Kind
    RoleFirst,                 ///< top: first volume's name, lower case
};
enum Kind { KindArchive = 0, KindEntry };

QString dirOf(const QString& path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash < 0 ? QString() : path.left(slash + 1);
}

} // namespace

bool ArchiveContentsView::opensArchive(const QString& path)
{
    // .part1.rar, a lone .rar (not .partN.rar), .zip, .7z, .7z.001, .001
    static const QRegularExpression re(QStringLiteral(R"((\.part0*1\.rar|(?<!\.part\d)(?<!\.part\d\d)\.rar|\.zip|\.7z|\.7z\.0*1|\.001)$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(path).hasMatch();
}

ArchiveContentsView::ArchiveContentsView(QWidget* parent, bool archiveFallback)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_status = new QLabel(tr("What the archives hold is looked up in ed3k, without downloading them."), this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    auto* row = new QHBoxLayout;
    row->addWidget(FileCheckButtons::create(this, [this](FileCheckButtons::Bulk mode) {
        FileCheckButtons::applyToLeaves(m_tree, ColName, mode);
        updateButtons();
    }));
    row->addStretch(1);
    m_again = new QPushButton(tr("Look Up Again"), this);
    connect(m_again, &QPushButton::clicked, this, &ArchiveContentsView::lookUp);
    row->addWidget(m_again);
    layout->addLayout(row);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({tr("Name"), tr("Size"), tr("Status")});
    m_tree->header()->setStretchLastSection(true);
    m_tree->setColumnWidth(ColName, 360);
    m_tree->setColumnWidth(ColSize, 90);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ArchiveContentsView::showMenu);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem*, int column) {
        if (column == ColName)
            updateButtons();
    });
    layout->addWidget(m_tree, 1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_downloadEntries = new QPushButton(tr("Download Checked from ed3k"), this);
    m_downloadEntries->setToolTip(tr("Each checked file is downloaded by its ed3k hash, without the archive."));
    connect(m_downloadEntries, &QPushButton::clicked, this, [this] { emit downloadEntriesRequested(checkedEntries()); });
    buttons->addWidget(m_downloadEntries);
    m_downloadArchive = new QPushButton(tr("Download Archive, Extract Checked"), this);
    m_downloadArchive->setToolTip(tr("The archive is downloaded as usual; only the checked files are extracted."));
    m_downloadArchive->setVisible(archiveFallback);
    connect(m_downloadArchive, &QPushButton::clicked, this, [this] { emit downloadArchiveRequested(checkedByArchive(), password()); });
    buttons->addWidget(m_downloadArchive);
    layout->addLayout(buttons);
    updateButtons();
}

void ArchiveContentsView::setQueued(bool queued)
{
    m_queued = queued;
    if (queued) {
        m_downloadArchive->setText(tr("Extract Only Checked"));
        m_downloadArchive->setToolTip(tr("When the download unpacks, only the checked files are extracted."));
        m_downloadArchive->setVisible(true);
    }
    updateButtons();
}

void ArchiveContentsView::ensureLookup()
{
    if (!m_requested)
        lookUp();
}

void ArchiveContentsView::reset()
{
    m_requested = false;
    m_tree->clear();
    m_nested.clear();
    m_password.clear();
    m_again->setEnabled(true);
    m_status->setText(tr("What the archives hold is looked up in ed3k, without downloading them."));
    updateButtons();
}

void ArchiveContentsView::setResult(const QCborMap& result)
{
    m_again->setEnabled(true);
    const QSignalBlocker block(m_tree);
    m_tree->clear();
    m_nested.clear();
    m_password.clear();
    const QCborArray archives = result.value(QStringLiteral("archives")).toArray();
    int found = 0;
    for (const QCborValue& v : archives) {
        const QCborMap a = v.toMap();
        auto* top = new QTreeWidgetItem(m_tree);
        top->setData(ColName, RoleKind, KindArchive);
        top->setData(ColName, RoleFirst, a.value(QStringLiteral("name")).toString().section(u'/', -1).toLower());
        fillArchive(top, a);
        // a queued download's choice so far
        if (a.contains(QStringLiteral("kept"))) {
            QSet<QString> kept;
            for (const QCborValue& p : a.value(QStringLiteral("kept")).toArray())
                kept.insert(p.toString());
            for (int k = 0; k < top->childCount(); ++k) {
                QTreeWidgetItem* c = top->child(k);
                if (!kept.contains(c->data(ColName, RolePath).toString()))
                    c->setCheckState(ColName, Qt::Unchecked);
            }
        }
        if (a.value(QStringLiteral("state")).toString() == QLatin1String("found")) {
            ++found;
            top->setExpanded(true);
            if (m_password.isEmpty())
                m_password = a.value(QStringLiteral("password")).toString();
        }
    }
    m_editable = result.value(QStringLiteral("editable")).toBool(true);
    if (archives.isEmpty())
        m_status->setText(tr("This release has no archives."));
    else if (found == 0)
        m_status->setText(tr("Nobody in ed3k has published what these archives hold yet."));
    else
        m_status->setText(tr("%n archive(s) listed by ed3k peers. Check the files you want.", nullptr, found)
                          + (m_password.isEmpty() ? QString() : u' ' + tr("Archive password: %1").arg(m_password)));
    if (m_queued && !m_editable)
        m_status->setText(m_status->text() + u' ' + tr("The download is unpacked already: its extraction cannot change."));
    updateButtons();
}

void ArchiveContentsView::setError(const QString& error)
{
    m_again->setEnabled(true);
    m_requested = false;   // another try when shown again
    m_status->setText(error.isEmpty() ? tr("The lookup failed.") : error);
    updateButtons();
}

void ArchiveContentsView::setNestedResult(const QString& rootHex, const QCborMap& result)
{
    QTreeWidgetItem* entry = m_nested.take(rootHex);
    if (!entry)
        return;
    const QCborArray archives = result.value(QStringLiteral("archives")).toArray();
    const QSignalBlocker block(m_tree);
    if (archives.isEmpty()) {
        entry->setText(ColStatus, tr("Not listed"));
        return;
    }
    fillArchive(entry, archives.first().toMap());
    entry->setExpanded(true);
    updateButtons();
}

void ArchiveContentsView::setNestedError(const QString& rootHex, const QString& error)
{
    if (QTreeWidgetItem* entry = m_nested.take(rootHex))
        entry->setText(ColStatus, error.isEmpty() ? tr("Lookup failed") : error);
}

QCborArray ArchiveContentsView::checkedEntries() const
{
    QCborArray out;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item) {
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* c = item->child(i);
            if (c->data(ColName, RoleKind).toInt() == KindEntry && c->childCount() == 0
                && c->checkState(ColName) == Qt::Checked)
                out.append(QCborMap{{QStringLiteral("root"), c->data(ColName, RoleRoot).toString()},
                                    {QStringLiteral("size"), c->data(ColName, RoleSize).toLongLong()},
                                    {QStringLiteral("path"), c->data(ColName, RolePath).toString()}});
            walk(c);
        }
    };
    walk(m_tree->invisibleRootItem());
    return out;
}

QHash<QString, QStringList> ArchiveContentsView::checkedByArchive() const
{
    QHash<QString, QStringList> out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = m_tree->topLevelItem(i);
        if (top->childCount() == 0 || top->checkState(ColName) == Qt::Checked)
            continue;
        QStringList paths;
        for (int k = 0; k < top->childCount(); ++k) {
            QTreeWidgetItem* c = top->child(k);
            // a nested archive with any file checked: the archive itself is extracted
            if (c->checkState(ColName) != Qt::Unchecked)
                paths << c->data(ColName, RolePath).toString();
        }
        out.insert(top->data(ColName, RoleFirst).toString(), paths);
    }
    return out;
}

QString ArchiveContentsView::password() const
{
    return m_password;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void ArchiveContentsView::lookUp()
{
    m_requested = true;
    m_again->setEnabled(false);
    m_status->setText(tr("Looking up the archives in ed3k... (up to 2 minutes)"));
    emit lookupRequested();
}

void ArchiveContentsView::fillArchive(QTreeWidgetItem* top, const QCborMap& archive)
{
    const bool nested = top->data(ColName, RoleKind).toInt() == KindEntry;
    if (!nested)
        top->setText(ColName, archive.value(QStringLiteral("name")).toString());
    top->setText(ColStatus, stateText(archive));
    const QCborArray entries = archive.value(QStringLiteral("entries")).toArray();
    if (entries.isEmpty())
        return;
    // below a nested archive: its files go beside it
    const QString base = nested ? dirOf(top->data(ColName, RolePath).toString()) : QString();
    qint64 total = 0;
    top->setFlags(top->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
    for (const QCborValue& v : entries) {
        const QCborMap e = v.toMap();
        const QString path = e.value(QStringLiteral("path")).toString();
        const qint64 size = e.value(QStringLiteral("size")).toInteger();
        total += size;
        auto* item = new QTreeWidgetItem(top);
        item->setText(ColName, path);
        item->setText(ColSize, formatByteSize(size));
        item->setData(ColName, RoleKind, KindEntry);
        item->setData(ColName, RoleRoot, e.value(QStringLiteral("root")).toString());
        item->setData(ColName, RolePath, QString(base + path));
        item->setData(ColName, RoleSize, size);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(ColName, Qt::Checked);
        if (opensArchive(path))
            item->setToolTip(ColName, tr("An archive: right-click to look inside."));
    }
    top->setText(ColSize, formatByteSize(total));
    top->setCheckState(ColName, Qt::Checked);
}

void ArchiveContentsView::updateButtons()
{
    const bool any = !checkedEntries().isEmpty();
    m_downloadEntries->setEnabled(any);
    bool listed = false;
    for (int i = 0; i < m_tree->topLevelItemCount() && !listed; ++i)
        listed = m_tree->topLevelItem(i)->childCount() > 0;
    m_downloadArchive->setEnabled(listed && any && (!m_queued || m_editable));
}

void ArchiveContentsView::showMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item || item->data(ColName, RoleKind).toInt() != KindEntry || item->childCount() > 0
        || !opensArchive(item->text(ColName)))
        return;
    QMenu menu(this);
    QAction* inside = menu.addAction(tr("Look Inside"));
    if (menu.exec(m_tree->viewport()->mapToGlobal(pos)) != inside)
        return;
    const QString root = item->data(ColName, RoleRoot).toString();
    m_nested.insert(root, item);
    item->setText(ColStatus, tr("Looking up..."));
    emit nestedLookupRequested(item->text(ColName).section(u'/', -1), root);
}

QString ArchiveContentsView::stateText(const QCborMap& archive)
{
    const QString state = archive.value(QStringLiteral("state")).toString();
    if (state == QLatin1String("found")) {
        const int sites = int(archive.value(QStringLiteral("sites")).toInteger());
        return sites > 0 ? tr("Listed by %n site(s)", nullptr, sites) : tr("Listed");
    }
    if (state == QLatin1String("notPublished"))
        return tr("Not listed yet");
    if (state == QLatin1String("noRoot"))
        return tr("Not known in ed3k");
    if (state == QLatin1String("timedOut"))
        return tr("No answer in time");
    return tr("Lookup failed");
}

} // namespace eMule

#include "pch.h"
/// @file NzbFileChooserDialog.cpp
/// @brief Pick which files of one or more .nzb download, before they are queued.

#include "dialogs/NzbFileChooserDialog.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/UsenetFileCheckList.h"
#include "utils/DialogSizing.h"
#include "utils/FileTypeIcons.h"

#include "utils/OtherFunctions.h"
#include "utils/StringUtils.h"

#include <QCborArray>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QVBoxLayout>

namespace eMule {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kInspectedRole = Qt::UserRole + 2;

} // namespace

NzbFileChooserDialog::NzbFileChooserDialog(IpcClient* ipc, const QStringList& paths,
                                           const QHash<QString, QList<int>>& skipped,
                                           QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
    , m_initial(skipped)
    , m_result(skipped)
{
    setWindowTitle(tr("Choose Files"));

    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Unchecked files are not downloaded. The volumes of one archive "
                               "are checked together, and PAR2 files are fetched only when a "
                               "repair needs them."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* tree = new ListTreeWidget(this);
    m_tree = tree;
    tree->setColumnCount(ColCount);
    tree->setHeaderLabels({tr("Name"), tr("Size")});
    tree->setRootIsDecorated(paths.size() > 1);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->bindColumns(QStringLiteral("nzbFileChooser"), {400, 90});

    m_checks = new UsenetFileCheckList(tree, ColName);
    layout->addWidget(m_checks->createButtonRow(this));
    layout->addWidget(tree, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &NzbFileChooserDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    for (const QString& path : paths) {
        auto* row = new QTreeWidgetItem(tree);
        row->setText(ColName, QFileInfo(path).fileName());
        row->setData(ColName, kPathRole, path);
        auto* pending = new QTreeWidgetItem(row);
        pending->setText(ColName, tr("Reading…"));
        row->setExpanded(true);
    }
    for (const QString& path : paths)
        inspect(path);

    DialogSizing::applySize(this, {}, QSize(600, 440), DialogSizing::Fit::Layout);
}

void NzbFileChooserDialog::applyInspection(const QString& path, const QCborMap& inspected)
{
    QTreeWidgetItem* row = nzbRow(path);
    if (!row)
        return;

    qDeleteAll(row->takeChildren());
    const QList<int> skipped = m_initial.value(path);
    const QBrush grey = palette().brush(QPalette::Disabled, QPalette::Text);

    qint64 total = 0;
    for (const QCborValue& value : inspected.value(QStringLiteral("files")).toArray()) {
        const QCborMap f = value.toMap();
        const int index = int(f.value(QStringLiteral("index")).toInteger(-1));
        const QString name = f.value(QStringLiteral("name")).toString();
        const qint64 size = f.value(QStringLiteral("size")).toInteger();
        const bool isPar2 = f.value(QStringLiteral("isPar2")).toBool();

        auto* item = new QTreeWidgetItem(row);
        item->setText(ColName, name);
        item->setIcon(ColName, fileTypeIconForName(name));
        item->setText(ColSize, formatByteSize(size));
        item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
        m_checks->bindRow(item, index, !skipped.contains(index), isPar2,
                          f.value(QStringLiteral("setKey")).toString());
        if (isPar2) {
            item->setToolTip(ColName, tr("PAR2 files are fetched when a repair needs them."));
            item->setForeground(ColName, grey);
            item->setForeground(ColSize, grey);
        }
        total += size;
    }

    row->setText(ColSize, formatByteSize(total));
    row->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
    row->setData(ColName, kInspectedRole, true);
    row->setExpanded(true);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void NzbFileChooserDialog::inspect(const QString& path)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        showError(path, tr("Not connected to the eMule core."));
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showError(path, tr("Cannot read %1.").arg(QFileInfo(path).fileName()));
        return;
    }

    // The bytes, as AddNzb sends them: the daemon may be on another machine.
    Ipc::IpcMessage msg(Ipc::IpcMsgType::InspectNzb);
    msg.append(QCborValue(file.readAll()));
    msg.append(QFileInfo(path).completeBaseName());

    const QPointer<NzbFileChooserDialog> self(this);
    m_ipc->sendRequest(std::move(msg), [self, path](const Ipc::IpcMessage& reply) {
        if (!self)
            return;
        if (!reply.isValid()) {
            self->showError(path, NzbFileChooserDialog::tr("Not connected to the eMule core."));
            return;
        }
        if (!reply.fieldBool(0)) {
            self->showError(path, reply.fieldString(1));
            return;
        }
        self->applyInspection(path, reply.fieldMap(1));
    });
}

void NzbFileChooserDialog::showError(const QString& path, const QString& text)
{
    QTreeWidgetItem* row = nzbRow(path);
    if (!row)
        return;
    qDeleteAll(row->takeChildren());
    auto* note = new QTreeWidgetItem(row);
    note->setText(ColName, text.isEmpty() ? tr("This file could not be read.") : text);
    note->setForeground(ColName, palette().brush(QPalette::Disabled, QPalette::Text));
    note->setFlags(Qt::ItemIsEnabled);
}

void NzbFileChooserDialog::onAccepted()
{
    QHash<QString, QList<int>> result;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_tree->topLevelItem(i);
        const QString path = row->data(ColName, kPathRole).toString();

        // Never read: keep what an earlier visit chose, which is nothing the first time.
        if (!row->data(ColName, kInspectedRole).toBool()) {
            if (m_initial.contains(path))
                result.insert(path, m_initial.value(path));
            continue;
        }

        const QList<int> unchecked = m_checks->uncheckedIndices(row);
        if (unchecked.isEmpty())
            continue;
        // The daemon refuses the add otherwise; saying so here keeps the choice open.
        if (!m_checks->anyUnlockedChecked(row)) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("Keep at least one file of \"%1\".").arg(row->text(ColName)));
            return;
        }
        result.insert(path, unchecked);
    }

    m_result = result;
    accept();
}

QTreeWidgetItem* NzbFileChooserDialog::nzbRow(const QString& path) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->data(ColName, kPathRole).toString() == path)
            return m_tree->topLevelItem(i);
    }
    return nullptr;
}

} // namespace eMule

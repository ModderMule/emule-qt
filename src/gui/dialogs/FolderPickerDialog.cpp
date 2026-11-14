#include "pch.h"
/// @file FolderPickerDialog.cpp
/// @brief A folder on the core's machine — implementation.

#include "dialogs/FolderPickerDialog.h"

#include "controls/FolderTreeModel.h"
#include "utils/DialogSizing.h"
#include "utils/FolderLister.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

FolderPickerDialog::FolderPickerDialog(IpcClient* ipc, const QString& title, const QString& startDir,
                                       QWidget* parent)
    : RememberedSizeDialog(parent)
    , m_model(new FolderTreeModel(ipc, false, this))
    , m_tree(new QTreeView(this))
    , m_pathEdit(new QLineEdit(this))
{
    setWindowTitle(title);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Folder on the core's machine:"), this));
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    layout->addWidget(m_tree, 1);
    m_pathEdit->setText(QDir::toNativeSeparators(startDir));
    layout->addWidget(m_pathEdit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    const auto updateOk = [this, buttons] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(QDir::isAbsolutePath(selectedFolder()));
    };
    connect(m_pathEdit, &QLineEdit::textChanged, this, updateOk);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& idx) {
        if (idx.isValid())
            m_pathEdit->setText(QDir::toNativeSeparators(m_model->filePath(idx)));
    });
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (idx.isValid() && !m_model->hasChildren(idx))
            accept();
    });
    connect(m_model, &FolderTreeModel::revealed, this, [this](const QModelIndex& idx) {
        const QString typed = m_pathEdit->text();
        m_tree->scrollTo(idx, QAbstractItemView::PositionAtCenter);   // expands the folders above
        m_tree->setCurrentIndex(idx);
        if (!typed.isEmpty())
            m_pathEdit->setText(typed);   // a typed folder that does not exist yet stays
    });
    if (startDir.isEmpty())
        connect(m_model, &FolderTreeModel::homeKnown, this, [this](const QString& home) { m_model->reveal(home); });
    else
        m_model->reveal(startDir);
    if (!m_model->homePath().isEmpty() && startDir.isEmpty())
        m_model->reveal(m_model->homePath());   // a local core answered in the constructor
    updateOk();

    DialogSizing::applySize(this, {420, 380}, {520, 480}, DialogSizing::Fit::Layout);
}

QString FolderPickerDialog::selectedFolder() const
{
    const QString text = m_pathEdit->text().trimmed();
    return text.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(text));
}

QString FolderPickerDialog::getExistingDirectory(QWidget* parent, IpcClient* ipc, const QString& title,
                                                 const QString& startDir)
{
    if (!FolderLister::remote(ipc))
        return QDir::fromNativeSeparators(QFileDialog::getExistingDirectory(parent, title, startDir));
    FolderPickerDialog dlg(ipc, title, startDir, parent);
    return dlg.exec() == QDialog::Accepted ? dlg.selectedFolder() : QString();
}

} // namespace eMule

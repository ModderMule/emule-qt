#include "pch.h"
/// @file FindInListDialog.cpp
/// @brief Shared "Find..." dialog — see FindInListDialog.h.

#include "FindInListDialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>

namespace eMule {

void showFindInListDialog(QWidget* parent, QAbstractItemView* view)
{
    auto* model = view ? view->model() : nullptr;
    if (!model)
        return;

    QDialog dlg(parent);
    dlg.setWindowTitle(QCoreApplication::translate("eMule::FindInListDialog", "Search"));

    auto* layout = new QFormLayout(&dlg);

    // Spell the context out at every call: lupdate resolves a tr()-shaped
    // lambda against the enclosing namespace and would file these strings
    // under context "eMule", where the runtime lookup never finds them.
    auto* searchEdit = new QLineEdit(&dlg);
    layout->addRow(QCoreApplication::translate("eMule::FindInListDialog", "Search for:"),
                   searchEdit);

    // Column names come from the view's own header, so each list offers exactly
    // the columns it shows — including any the user has since renamed or hidden.
    auto* columnCombo = new QComboBox(&dlg);
    for (int col = 0; col < model->columnCount(); ++col) {
        const QString label = model->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
        if (!label.isEmpty())
            columnCombo->addItem(label, col);
    }
    layout->addRow(QCoreApplication::translate("eMule::FindInListDialog", "Search in column:"),
                   columnCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString term = searchEdit->text().trimmed();
    if (term.isEmpty())
        return;

    const int column = columnCombo->currentData().toInt();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex idx = model->index(row, column);
        if (idx.data(Qt::DisplayRole).toString().contains(term, Qt::CaseInsensitive)) {
            view->setCurrentIndex(idx);
            view->scrollTo(idx);
            return;
        }
    }
}

} // namespace eMule

#include "pch.h"
/// @file FindInListDialog.cpp
/// @brief Shared "Find..." dialog — see FindInListDialog.h.

#include "FindInListDialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMessageBox>

namespace eMule {

namespace {

/// Last term/column per view, parented to the view so it dies with it.
class FindState : public QObject {
public:
    explicit FindState(QAbstractItemView* view) : QObject(view)
    {
        setObjectName(QString::fromLatin1(kName));
    }

    static constexpr const char* kName = "eMuleFindInListState";

    QString term;
    int     column = 0;
};

FindState* findState(QAbstractItemView* view, bool create)
{
    // By name, not type: FindState has no Q_OBJECT, which findChild<T> requires.
    auto* state = static_cast<FindState*>(view->findChild<QObject*>(
        QString::fromLatin1(FindState::kName), Qt::FindDirectChildrenOnly));
    if (!state && create)
        state = new FindState(view);
    return state;
}

/// Drop all whitespace: cells pad for display ("1.2.3.4 : 4661"), a typed or
/// pasted term doesn't ("1.2.3.4:4661").
QString compact(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        if (!ch.isSpace())
            out += ch;
    }
    return out;
}

/// No match: the dialog says so, F3 only beeps — MFC DoFind bShowError.
void reportNoMatch(QWidget* parent, bool showError)
{
    if (!showError) {
        QApplication::beep();
        return;
    }
    QMessageBox::information(parent,
                             QCoreApplication::translate("eMule::FindInListDialog", "Search"),
                             QCoreApplication::translate("eMule::FindInListDialog",
                                                         "No matching entry found."));
}

/// Row to search after: the selected current row (children count as their
/// top-level parent), else just before the top / after the bottom — MFC
/// DoFindNext starts after the selected+focused item, else at row 0.
int searchStartRow(const QAbstractItemView* view, bool backwards)
{
    QModelIndex current = view->currentIndex();
    const auto* selection = view->selectionModel();
    if (current.isValid() && selection && selection->isSelected(current)) {
        while (current.parent().isValid())
            current = current.parent();
        return current.row();
    }
    return backwards ? view->model()->rowCount() : -1;
}

/// Select the first row after @p startRow (stepping by @p step, wrapping) whose
/// @p column text contains @p term, ignoring whitespace. startRow == -1 with
/// step 1 starts at the top.
bool selectMatch(QWidget* parent, QAbstractItemView* view, const QString& rawTerm, int column,
                 int startRow, int step, bool showError)
{
    auto* model = view->model();
    const int rows = model ? model->rowCount() : 0;
    const QString term = compact(rawTerm);
    if (rows <= 0 || column >= model->columnCount() || term.isEmpty()) {
        reportNoMatch(parent, showError);
        return false;
    }
    for (int i = 1; i <= rows; ++i) {
        const int row = ((startRow + step * i) % rows + rows) % rows;
        const QModelIndex idx = model->index(row, column);
        if (compact(idx.data(Qt::DisplayRole).toString()).contains(term, Qt::CaseInsensitive)) {
            view->setCurrentIndex(idx);
            view->scrollTo(idx);
            return true;
        }
    }
    reportNoMatch(parent, showError);
    return false;
}

} // anonymous namespace

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
    const FindState* prev = findState(view, false);
    if (prev) {
        // Last term, all selected: Enter repeats it, typing replaces it (MFC).
        searchEdit->setText(prev->term);
        searchEdit->selectAll();
    }
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
    // A headerless one-column list (the friends QListView) has nothing to choose.
    if (columnCombo->count() == 0)
        columnCombo->addItem(QString(), 0);
    if (columnCombo->count() == 1)
        layout->setRowVisible(columnCombo, false);
    if (prev) {
        const int idx = columnCombo->findData(prev->column);
        if (idx >= 0)
            columnCombo->setCurrentIndex(idx);
    }

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
    auto* state = findState(view, true);
    state->term = term;
    state->column = column;
    // From the selected row on, not the top, so a repeat finds the next match.
    selectMatch(parent, view, term, column, searchStartRow(view, false), 1, true);
}

void findNextInList(QWidget* parent, QAbstractItemView* view, bool backwards)
{
    if (!view || !view->model())
        return;
    const FindState* state = findState(view, false);
    if (!state || state->term.isEmpty()) {
        showFindInListDialog(parent, view);
        return;
    }
    selectMatch(parent, view, state->term, state->column, searchStartRow(view, backwards),
                backwards ? -1 : 1, false);
}

} // namespace eMule

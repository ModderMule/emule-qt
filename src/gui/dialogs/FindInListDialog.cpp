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
#include <QLineEdit>

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

/// Select the first row after @p startRow (stepping by @p step, wrapping) whose
/// @p column text contains @p term, ignoring whitespace. startRow == -1 with
/// step 1 starts at the top. Beeps on no match, as MFC DoFind does.
bool selectMatch(QAbstractItemView* view, const QString& rawTerm, int column, int startRow, int step)
{
    auto* model = view->model();
    const int rows = model ? model->rowCount() : 0;
    const QString term = compact(rawTerm);
    if (rows <= 0 || column >= model->columnCount() || term.isEmpty()) {
        QApplication::beep();
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
    QApplication::beep();
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
    selectMatch(view, term, column, -1, 1);
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
    // A source/child row counts as its top-level parent: find only walks top level.
    QModelIndex current = view->currentIndex();
    while (current.parent().isValid())
        current = current.parent();
    const int rows = view->model()->rowCount();
    const int start = current.isValid() ? current.row() : (backwards ? rows : -1);
    selectMatch(view, state->term, state->column, start, backwards ? -1 : 1);
}

} // namespace eMule

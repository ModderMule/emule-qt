/// @file HosterCombo.cpp
/// @brief Site box of the Add account dialogs — implementation.

#include "pch.h"
#include "controls/HosterCombo.h"

#include "utils/CountryFlags.h"
#include "utils/HosterIcons.h"

#include <QAction>
#include <QCompleter>
#include <QLineEdit>
#include <QListView>
#include <QStyledItemDelegate>

#include <algorithm>

namespace eMule {

namespace {

constexpr int kVisibleRows = 15;

/// A domain as stored: lower case, no "www.".
QString normalized(const QString& text)
{
    QString h = text.trimmed().toLower();
    if (h.startsWith(QLatin1StringView("www.")))
        h = h.mid(4);
    return h;
}

/// Favicons in the lists. Not in the model: the combo would draw the current one in the
/// edit field too, next to the leading icon of a typed site.
class IconDelegate : public QStyledItemDelegate {
public:
    IconDelegate(HosterIcons* icons, QObject* parent)
        : QStyledItemDelegate(parent)
        , m_icons(icons)
    {
    }

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (!m_icons)
            return;
        // asked only for painted rows: favicon and country are fetched then
        option->icon = m_icons->iconWithFlag(index.data(Qt::EditRole).toString());
        option->features |= QStyleOptionViewItem::HasDecoration;
        // the flag slot is kept without a country: hosts stay aligned
        option->decorationSize = QSize(CountryFlags::showFlags() ? 36 : 16, 16);
    }

private:
    QPointer<HosterIcons> m_icons;
};

void tuneList(QAbstractItemView* view, HosterIcons* icons)
{
    // 1000+ rows: only the visible ones are measured
    if (auto* lv = qobject_cast<QListView*>(view))
        lv->setUniformItemSizes(true);
    view->setItemDelegate(new IconDelegate(icons, view));
}

} // namespace

HosterCombo::HosterCombo(IpcClient* ipc, Kind kind, const QList<Known>& known, QWidget* parent)
    : QComboBox(parent)
    , m_icons(HosterIcons::of(ipc))
    , m_kind(kind)
    , m_known(known)
    , m_model(new Model(this))
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    // not every row's icon and text for the size: they come lazily
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(28);
    setMaxVisibleItems(kVisibleRows);
    setModel(m_model);
    tuneList(view(), m_icons);
    lineEdit()->setPlaceholderText(kind == Kind::Hosters ? tr("Type to search, e.g. rapidgator.net") : tr("Type to search"));
    lineEdit()->setClearButtonEnabled(true);
    m_iconAction = lineEdit()->addAction(QIcon(), QLineEdit::LeadingPosition);

    auto* completer = new QCompleter(m_model, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setMaxVisibleItems(kVisibleRows);
    tuneList(completer->popup(), m_icons);
    setCompleter(completer);

    connect(this, &QComboBox::editTextChanged, this, [this] {
        updateLeadingIcon();
        emit hostChanged(host());
    });
    if (m_icons) {
        connect(m_icons, &HosterIcons::iconChanged, this, [this](const QString& h) {
            m_model->iconChanged(h);
            if (h == host())
                updateLeadingIcon();
        });
        connect(m_icons, &HosterIcons::accountHostsChanged, this, &HosterCombo::rebuild);
    }
    rebuild();
}

QString HosterCombo::host() const
{
    const QString h = normalized(currentText());
    if (h.isEmpty() || m_model->rowOf(h) >= 0)
        return h;
    // a plugin's name typed ("Real-Debrid"): its domain
    for (int r = 0; r < m_model->rowCount(); ++r)
        if (m_model->at(r).name.compare(currentText().trimmed(), Qt::CaseInsensitive) == 0)
            return m_model->at(r).host;
    return h;
}

void HosterCombo::setHost(const QString& host)
{
    const QString h = normalized(host);
    const int row = m_model->rowOf(h);
    if (row >= 0)
        setCurrentIndex(row);
    setEditText(h);
}

// ---- private ----

void HosterCombo::rebuild()
{
    QHash<QString, Row> rows;
    for (const Known& k : std::as_const(m_known))
        rows.insert(k.host, {k.host, k.name});
    if (m_icons) {
        for (const HosterIcons::AccountHost& a : m_icons->accountHosts()) {
            const bool take = m_kind == Kind::Solvers        ? a.solver
                              : m_kind == Kind::MultiHosters ? a.multi
                                                             : !a.solver && !a.multi;
            if (take && !rows.contains(a.host))
                rows.insert(a.host, {a.host, a.name});
        }
    }
    QList<Row> list = rows.values();
    std::ranges::sort(list, {}, &Row::host);
    const QString text = currentText();
    const QSignalBlocker block(this);
    m_model->setRows(std::move(list));
    setCurrentIndex(-1);
    setEditText(text);
    updateLeadingIcon();
}

void HosterCombo::updateLeadingIcon()
{
    // only a listed site: a half typed domain gets no lettered icon
    const QString h = host();
    m_iconAction->setIcon(m_icons && m_model->rowOf(h) >= 0 ? m_icons->icon(h) : QIcon());
}

// ---- Model ----

HosterCombo::Model::Model(HosterCombo* combo)
    : QAbstractListModel(combo)
    , m_combo(combo)
{
}

int HosterCombo::Model::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant HosterCombo::Model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row& r = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return r.host;
    case Qt::ToolTipRole: {
        QString tip = r.name.isEmpty() ? r.host : r.name;
        if (const QString cc = m_combo->m_icons ? m_combo->m_icons->country(r.host) : QString(); !cc.isEmpty())
            tip += u'\n' + HosterCombo::tr("Server: %1").arg(CountryFlags::tooltip(cc));
        return tip;
    }
    default:
        return {};
    }
}

void HosterCombo::Model::setRows(QList<Row> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    m_index.clear();
    for (int i = 0; i < m_rows.size(); ++i)
        m_index.insert(m_rows.at(i).host, i);
    endResetModel();
}

void HosterCombo::Model::iconChanged(const QString& host)
{
    if (const int r = rowOf(host); r >= 0)
        emit dataChanged(index(r), index(r));   // the delegate paints it again
}

} // namespace eMule

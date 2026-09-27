#pragma once

/// @file CountryFlags.h
/// @brief Country flag icons and names for the "cc" field the daemon puts on client,
///        server, Kad contact and friend rows (MorphXT/EastShare IP2Country).
///
/// The flag sits right after the row's own icon in the name column, as in MorphXT.
/// The optional Country column shows the name, and its visibility follows the
/// Options setting rather than the saved header layout.
///
/// Settings are held here (seeded from UiState at startup) so the list models stay
/// free of UiState and compile standalone in the GUI tests.

#include <QAbstractItemView>
#include <QHeaderView>
#include <QIcon>
#include <QObject>
#include <QString>
#include <QStyledItemDelegate>

namespace eMule {

/// Emits changed() when the flag settings change. Singleton.
class CountryFlagsNotifier : public QObject {
    Q_OBJECT

public:
    static CountryFlagsNotifier* instance();

signals:
    void changed();

private:
    using QObject::QObject;
};

/// Lets a composed "icon + flag" decoration keep its width instead of being
/// squeezed into the view's square icon size.
class FlagDecorationDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;
};

namespace CountryFlags {

/// Country column content; Hidden also hides the column. Persisted in UiState.
enum class NameMode : int { Hidden = 0, Short = 1, Long = 2 };

[[nodiscard]] bool showFlags();
[[nodiscard]] NameMode nameMode();

/// Update the settings and repaint every bound view.
void setSettings(bool showFlags, NameMode mode);

/// Repaint every bound view (e.g. after a new database arrived).
void notifyChanged();

/// Flag for an ISO 3166-1 alpha-2 code (any case); null when unknown or empty.
[[nodiscard]] QIcon flag(const QString& cc);

/// English long name ("Germany"), or the code itself when Qt doesn't know it.
[[nodiscard]] QString countryName(const QString& cc);

/// Country column text for the current mode.
[[nodiscard]] QString columnText(const QString& cc);

/// Sort key for a Country column: the displayed text.
[[nodiscard]] inline QString sortKey(const QString& cc) { return columnText(cc); }

/// "Germany (DE)", empty when there is no country.
[[nodiscard]] QString tooltip(const QString& cc);

/// @p base with the flag appended (16 + 2 + 18 px). Returns @p base when flags are
/// off. The flag slot is kept even without a country so names stay aligned.
/// A null @p base yields just the flag slot.
[[nodiscard]] QIcon withFlag(const QIcon& base, const QString& cc);

/// Widen column 0's decoration for the flag and repaint on setting changes.
void bindFlagColumn(QAbstractItemView* view);

/// bindFlagColumn() plus the Country column: hidden or shown per nameMode(), also
/// after a saved layout is restored. @p View is a ListTreeView / ListTreeWidget.
template<class View>
void bindCountryColumn(View* view, int column)
{
    view->setColumnPolicy([column](QHeaderView* header) {
        header->setSectionHidden(column, nameMode() == NameMode::Hidden);
    });
    bindFlagColumn(view);
    QObject::connect(CountryFlagsNotifier::instance(), &CountryFlagsNotifier::changed, view,
                     [view] { view->applyColumnPolicy(); });
}

} // namespace CountryFlags

} // namespace eMule

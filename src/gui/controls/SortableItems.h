#pragma once

/// @file SortableItems.h
/// @brief List rows that sort by the value behind a cell, not by the text in it.
///
/// Qt orders an item-backed list by its *display string*: QTreeWidgetItem's
/// operator< compares data(column, Qt::DisplayRole), and QStandardItemModel's
/// sortRole defaults to the same. So a column showing "9.90 MB" ranks above
/// "10.00 GB", "7%" above "100%", and "950" above "10000".
///
/// Stashing the raw number under Qt::UserRole does not help -- nothing reads it.
/// Five dialogs in this GUI did exactly that, each with a comment claiming it
/// worked. These two item classes are what makes such a comment true.
///
/// Header-only and deliberately free of Q_OBJECT: nothing here has signals, so
/// there is no moc step and no *_autogen cache to clear when it is added to a
/// target.

#include <QDateTime>
#include <QMetaType>
#include <QStandardItem>
#include <QString>
#include <QTreeWidget>
#include <QVariant>

namespace eMule {

/// The role a numeric column puts its magnitude under.
///
/// Deliberately **not** Qt::UserRole. Every list here already uses that role for
/// a payload -- an archive entry ordinal, an IRC channel name, a published file
/// path, a collection hash -- and a comparator reading Qt::UserRole would
/// silently sort by the payload instead of by the cell. That is not theoretical:
/// it is what made the Usenet details dialog's Name column stop sorting.
constexpr int SortRole = Qt::UserRole + 100;

/// Order two sort keys by what they are rather than by how they print.
[[nodiscard]] inline bool sortValueLessThan(const QVariant& a, const QVariant& b)
{
    const int ta = a.typeId();
    const int tb = b.typeId();

    if (ta == QMetaType::QDateTime || tb == QMetaType::QDateTime)
        return a.toDateTime() < b.toDateTime();

    // QString before the numeric branches, never after: QVariant(QString) answers
    // canConvert<qlonglong>() with true and toLongLong() with 0, so a text key
    // reached through a numeric path collapses the whole column into "all equal".
    if (ta == QMetaType::QString || tb == QMetaType::QString)
        return QString::localeAwareCompare(a.toString(), b.toString()) < 0;

    if (ta == QMetaType::Double || tb == QMetaType::Double
        || ta == QMetaType::Float || tb == QMetaType::Float)
        return a.toDouble() < b.toDouble();

    return a.toLongLong() < b.toLongLong();
}

/// A QTreeWidget row that sorts on SortRole where it is set.
///
/// A column without one falls back to the inherited text comparison, so a mixed
/// table needs the role only on the columns that are numbers -- and a Name column
/// keeps sorting as a name.
class SortableTreeItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem& other) const override
    {
        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant mine = data(column, SortRole);
        const QVariant theirs = other.data(column, SortRole);
        if (mine.isValid() && theirs.isValid())
            return sortValueLessThan(mine, theirs);
        return QTreeWidgetItem::operator<(other);
    }
};

/// The same for a QStandardItemModel bound straight to a view.
///
/// No column lookup: sortChildren() only ever compares two items in the same
/// column, so the item's own SortRole is the right key.
class SortableStandardItem : public QStandardItem {
public:
    using QStandardItem::QStandardItem;

    bool operator<(const QStandardItem& other) const override
    {
        const QVariant mine = data(SortRole);
        const QVariant theirs = other.data(SortRole);
        if (mine.isValid() && theirs.isValid())
            return sortValueLessThan(mine, theirs);
        return QStandardItem::operator<(other);
    }
};

} // namespace eMule

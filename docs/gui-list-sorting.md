# Sorting a list column that holds numbers

Qt orders a list by the **display string**. That is the whole problem: a Size
column showing `9.9 MiB` ranks above `10.0 GiB`, a Progress column puts `7%`
above `100%`, and an IRC channel with 950 users outranks one with 10 000.

Writing the raw value to `Qt::UserRole` does **not** fix it. Nothing reads that
role unless something is told to. Five dialogs in this GUI carried a line like

```cpp
item->setData(ColSize, Qt::UserRole, size);   // sort numerically, not as text
```

and in every one of them the comment was false.

## The three shapes, and what each needs

| the list is | Qt sorts it via | the fix |
|---|---|---|
| `QTreeWidget` + `QTreeWidgetItem` | `QTreeWidgetItem::operator<` on `Qt::DisplayRole` | `SortableTreeItem` + a `SortRole` key |
| `QTreeView` + a bare `QStandardItemModel` | `QStandardItem::operator<` on the model's `sortRole()`, which defaults to `Qt::DisplayRole` | `SortableStandardItem` + a `SortRole` key |
| `QTreeView` + `QSortFilterProxyModel` | `lessThan()` on the proxy's `sortRole()` | model answers `Qt::UserRole` with the raw value; panel calls `proxy->setSortRole(Qt::UserRole)` |

Sorting is only ever live where an explicit `setSortingEnabled(true)` appears.
`AbstractListView::bindColumns()` does not enable it — it restores the saved
column layout, sort indicator included, which is a different thing.

## Item-backed lists — `controls/SortableItems.h`

```cpp
auto* item = new SortableTreeItem(tree);
item->setText(ColSize, QLocale::system().formattedDataSize(bytes));
item->setData(ColSize, SortRole, bytes);        // the magnitude behind the cell
item->setData(ColName, Qt::UserRole, path);     // an unrelated payload; ignored by the sort
```

`SortRole` is `Qt::UserRole + 100`, and the distance matters. Every one of these
lists already spends `Qt::UserRole` on something a double-click handler reads
back — an archive entry ordinal, an IRC channel name, a published path, a
collection hash. A comparator reading `Qt::UserRole` would sort the Name column
by that payload; when the payload is a string, `toLongLong()` answers 0 on both
sides, every pair compares equal, and the column silently stops sorting. That is
not hypothetical — it is what the Usenet details dialog shipped with.

A column with no `SortRole` falls back to the inherited text comparison, so give
the role only to the columns that are numbers and a Name column keeps sorting as
a name. `sortValueLessThan()` handles `QDateTime`, text, floating point and
integers, in that order; the string test must come first, because
`QVariant(QString).canConvert<qlonglong>()` is true.

## Model-backed lists

Answer `Qt::UserRole` with the raw value for the formatted columns and let the
rest fall through, as `IndexerResultsModel::data()` does:

```cpp
if (role == Qt::UserRole) {
    switch (index.column()) {
    case ColSize:   return QVariant::fromValue(row->size);
    ...
    default:        return data(index, Qt::DisplayRole);   // text columns
    }
}
```

The `default:` line is not optional. With `setSortRole(Qt::UserRole)` a column
that answers nothing returns an invalid variant on both sides, every row compares
equal, and that column stops sorting.

Where a column shows a **word** for an ordinal — a status, a priority, a server
preference — the key is a deliberate rank, not the underlying enum. Wire values
are wire order: the Usenet status enum puts Paused between Downloading and
Complete, and the server preference field is 0 Normal, 1 High, 2 Low. See
`UsenetQueueModel::statusRank()`, `DownloadListModel::statusRank()`,
`SharedFilesModel::priorityOrdinal()`.

Two more traps in that family:

- An address sorts by leading digit unless each octet is padded —
  `192.168.1.10` lands before `192.168.1.9`. `ServerListModel`'s
  `addressSortKey()` pads octets and port.
- A sentinel is a third state, not an extreme. The Usenet Health column shows an
  em dash for `-1` ("never assessed"), which as text sorts after every digit and
  parks unchecked releases at the healthy end; the sort key keeps the `-1`.

## Testing it

`tests/tst_ListSorting.cpp` covers both halves — the item classes and the three
models. The rule every fixture there follows: **pick values whose text order is
the reverse of the right order**, and insert them in a third order again. A
fixture that sorts the same either way passes against the bug. That is not
theoretical either: `tst_UsenetArchiveEntryDialog` sorted 500 / 1000 / 900000
bytes and passed for years because `"879.0 KiB"` happens to lead `"1000 bytes"`
on a text compare.

## Lists deliberately left sorting as text

`MetadataPage` (Tag Name / Type / Value — heterogeneous by nature),
`KadContactsModel`'s Distance (a fixed-width binary string from the daemon, for
which lexicographic order *is* numeric order), and every list where
`setSortingEnabled` is never called: the `OptionsDialog` tables,
`SharedFilesPanel::m_folderTree`, `StatisticsPanel`, `ImportDownloadsDialog`.
Several of those hold formatted columns and would need a key the day their
headers become clickable.

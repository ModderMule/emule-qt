# List columns

Every column-based list (panels and dialogs) is a `ListTreeView` / `ListTreeWidget`
(`src/gui/controls/AbstractListView.h`) and binds its header with `bindColumns()`.

## Header menu

Right-click a list header to show or hide columns, as in MFC's `CMuleListCtrl`:

- One checkable entry per column, labelled with the column caption. Checked means visible.
- The first column is left out and can't be hidden.
- No title or extra entries.
- Showing a column restores its previous width. A column that was hidden from the start gets
  its default width.

The menu is built each time it opens, so lists that swap models (Search: eD2K vs indexer) always
list the current columns. `setLockedColumns()` leaves out columns that have no data yet (the
indexer's Seeders and Peers).

## Persistence

Visibility is part of the header layout that `UiState` saves in `uistate.yml` (`headers:`), along
with widths, order and sort. A toggle is saved right away via `UiState::captureHeaderState()`.

## Hidden by default

`bindColumns(key, widths, defaultHidden)` hides columns only when there is no saved layout,
like MFC's `InsertColumn(..., bHiddenByDefault)`. A saved layout always wins.

| List | Hidden by default |
|---|---|
| Downloads | Last Seen Complete, Last Reception, Category, Country |
| Client lists (upload, download, queue, known) | Country |
| Server list | Country |
| Kad contacts | Country |
| Shared files | Folder |

MFC also hides columns we don't have (Transferred, File ID, AICH hash, Hard files, Version,
media columns in Shared files).

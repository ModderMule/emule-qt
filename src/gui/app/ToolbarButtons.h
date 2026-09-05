#pragma once

/// @file ToolbarButtons.h
/// @brief The single definition of the toolbar's buttons and their default order.
///
/// This used to be four tables in two files: `kAllButtons` + `kDefaultToolbarOrder`
/// in MainWindow.cpp, and a trimmed `kButtonMeta` + a verbatim copy of the order in
/// ToolbarCustomizeDialog.cpp, the latter carrying the comment "Must match
/// kAllButtons in MainWindow.cpp". Adding one button meant four hand-synced edits,
/// and a missed one shows up as a button labelled "Unknown" with no icon that
/// cannot be re-added once removed. One table, one place.
///
/// The dialog ignores `fallback` and `tabIndex`; it needs only the label and the
/// icon. That is cheaper than keeping a second struct in step.

#include <QList>
#include <QStyle>

namespace eMule {

/// Identifies each toolbar button for customization persistence.
///
/// The values are **persisted as raw ints** in uistate.yml's `toolbarButtonOrder`,
/// so they may never be renumbered. Tab buttons occupy the 10-19 block, non-tab
/// actions the 20s.
enum class ToolbarButtonId : int {
    Connect = 0,
    Separator = 1,
    Kad = 10, Servers = 11, Transfers = 12, Search = 13,
    SharedFiles = 14, Messages = 15, IRC = 16, Statistics = 17,
    Usenet = 18,
    Options = 20, Tools = 21, Help = 22, DownloadsFolder = 23,
};

struct ToolbarButtonDef {
    ToolbarButtonId id;
    const char* label;
    const char* iconResource;
    QStyle::StandardPixmap fallback;
    int tabIndex; // -1 if not a tab button
};

/// `tabIndex` is the QStackedWidget position set up by MainWindow::setupPages(),
/// so the two are positional partners: a page inserted anywhere but the end
/// renumbers every literal below.
inline constexpr ToolbarButtonDef kAllButtons[] = {
    {ToolbarButtonId::Connect,     "Connect",      "ConnectDrop.ico", QStyle::SP_MediaStop,               -1},
    {ToolbarButtonId::Kad,         "Kad",           "Kad.ico",        QStyle::SP_DriveNetIcon,             0},
    {ToolbarButtonId::Servers,     "Servers",       "Server.ico",     QStyle::SP_ComputerIcon,             1},
    {ToolbarButtonId::Transfers,   "Transfers",     "Transfer.ico",   QStyle::SP_ArrowDown,                2},
    {ToolbarButtonId::Search,      "Search",        "Search.ico",     QStyle::SP_FileDialogContentsView,   3},
    {ToolbarButtonId::SharedFiles, "Shared Files",  "SharedFiles.ico",QStyle::SP_DirOpenIcon,              4},
    {ToolbarButtonId::Messages,    "Messages",      "Messages.ico",   QStyle::SP_MessageBoxInformation,    5},
    {ToolbarButtonId::IRC,         "IRC",           "IRC.ico",        QStyle::SP_DialogApplyButton,        6},
    {ToolbarButtonId::Statistics,  "Statistics",     "Statistics.ico", QStyle::SP_DialogHelpButton,         7},
    {ToolbarButtonId::Usenet,      "Usenet",        "Usenet.ico",     QStyle::SP_DriveNetIcon,             8},
    {ToolbarButtonId::DownloadsFolder, "Downloads Folder", "DownloadsFolder.ico",
                                                    QStyle::SP_DirOpenIcon,             -1},
    {ToolbarButtonId::Options,     "Options",       "Preferences.ico",QStyle::SP_FileDialogDetailedView,  -1},
    {ToolbarButtonId::Tools,       "Tools",         "Tools.ico",      QStyle::SP_DialogResetButton,       -1},
    {ToolbarButtonId::Help,        "Help",          "Help.ico",       QStyle::SP_TitleBarContextHelpButton,-1},
};

/// Raw ints, matching what is stored in uistate.yml. Separator (1) appears twice,
/// so this is a multiset — never test membership by assuming uniqueness.
///
/// A user with a saved `toolbarButtonOrder` keeps it verbatim: nothing merges new
/// ids into an existing order, so a button added here reaches a customized install
/// only through Customize Toolbar. That is deliberate — the alternative re-adds a
/// button the user deliberately removed, on every restart.
inline const QList<int> kDefaultToolbarOrder = {
    0, 1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 1, 23, 20, 21, 22
};

[[nodiscard]] inline const ToolbarButtonDef* findButtonDef(ToolbarButtonId id)
{
    for (const auto& def : kAllButtons) {
        if (def.id == id)
            return &def;
    }
    return nullptr;
}

} // namespace eMule

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

#include <QCoreApplication>
#include <QList>
#include <QStyle>

#include <iterator>
#include <optional>

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
    Usenet = 18, Torrents = 19,
    Options = 20, Tools = 21, Help = 22, DownloadsFolder = 23,
    // the 10-19 tab block is full: a later tab takes the next free id
    Hoster = 24,
};

struct ToolbarButtonDef {
    ToolbarButtonId id;
    const char* label; // untranslated; tr() under eMule::MainWindow
    const char* iconResource;
    QStyle::StandardPixmap fallback;
    int tabIndex; // -1 if not a tab button
};

/// `tabIndex` is the QStackedWidget position set up by MainWindow::setupPages(),
/// so the two are positional partners: a page inserted anywhere but the end
/// renumbers every literal below.
inline constexpr ToolbarButtonDef kAllButtons[] = {
    {ToolbarButtonId::Connect,     QT_TRANSLATE_NOOP("eMule::MainWindow", "Connect"),      "ConnectDrop.ico", QStyle::SP_MediaStop,               -1},
    {ToolbarButtonId::Kad,         QT_TRANSLATE_NOOP("eMule::MainWindow", "Kad"),           "Kad.ico",        QStyle::SP_DriveNetIcon,             0},
    {ToolbarButtonId::Servers,     QT_TRANSLATE_NOOP("eMule::MainWindow", "Servers"),       "Server.ico",     QStyle::SP_ComputerIcon,             1},
    {ToolbarButtonId::Transfers,   QT_TRANSLATE_NOOP("eMule::MainWindow", "Transfers"),     "Transfer.ico",   QStyle::SP_ArrowDown,                2},
    {ToolbarButtonId::Search,      QT_TRANSLATE_NOOP("eMule::MainWindow", "Search"),        "Search.ico",     QStyle::SP_FileDialogContentsView,   3},
    {ToolbarButtonId::SharedFiles, QT_TRANSLATE_NOOP("eMule::MainWindow", "Shared Files"),  "SharedFiles.ico",QStyle::SP_DirOpenIcon,              4},
    {ToolbarButtonId::Messages,    QT_TRANSLATE_NOOP("eMule::MainWindow", "Messages"),      "Messages.ico",   QStyle::SP_MessageBoxInformation,    5},
    {ToolbarButtonId::IRC,         QT_TRANSLATE_NOOP("eMule::MainWindow", "IRC"),           "IRC.ico",        QStyle::SP_DialogApplyButton,        6},
    {ToolbarButtonId::Statistics,  QT_TRANSLATE_NOOP("eMule::MainWindow", "Statistics"),     "Statistics.ico", QStyle::SP_DialogHelpButton,         7},
    {ToolbarButtonId::Usenet,      QT_TRANSLATE_NOOP("eMule::MainWindow", "Usenet"),        "Usenet.ico",     QStyle::SP_DriveNetIcon,             8},
#ifdef EMULE_HAVE_BITTORRENT
    // Without it findButtonDef() misses 19: a saved order keeping it just skips it
    {ToolbarButtonId::Torrents,    QT_TRANSLATE_NOOP("eMule::MainWindow", "Torrents"),      "Torrent.ico",    QStyle::SP_DriveNetIcon,             9},
#endif
#ifdef EMULE_HAVE_HOSTER_JD
    {ToolbarButtonId::Hoster,      QT_TRANSLATE_NOOP("eMule::MainWindow", "Hoster"),        "Hoster.ico",     QStyle::SP_ArrowDown,                10},
#endif
    {ToolbarButtonId::DownloadsFolder, QT_TRANSLATE_NOOP("eMule::MainWindow", "Downloads Folder"), "DownloadsFolder.ico",
                                                    QStyle::SP_DirOpenIcon,             -1},
    {ToolbarButtonId::Options,     QT_TRANSLATE_NOOP("eMule::MainWindow", "Options"),       "Preferences.ico",QStyle::SP_FileDialogDetailedView,  -1},
    {ToolbarButtonId::Tools,       QT_TRANSLATE_NOOP("eMule::MainWindow", "Tools"),         "Tools.ico",      QStyle::SP_DialogResetButton,       -1},
    {ToolbarButtonId::Help,        QT_TRANSLATE_NOOP("eMule::MainWindow", "Help"),          "Help.ico",       QStyle::SP_TitleBarContextHelpButton,-1},
};

/// Raw ints, matching what is stored in uistate.yml. Separator (1) appears twice,
/// so this is a multiset — never test membership by assuming uniqueness.
///
/// A user with a saved `toolbarButtonOrder` keeps it verbatim: nothing merges new
/// ids into an existing order, so a button added here reaches a customized install
/// only through Customize Toolbar. That is deliberate — the alternative re-adds a
/// button the user deliberately removed, on every restart.
/// Exception: switching Torrents or Hoster on in Options puts its button back
/// (MainWindow::restoreToolbarButton), since that is a deliberate ask for it.
inline const QList<int> kDefaultToolbarOrder = {
    0, 1, 10, 11, 12, 13, 14, 15, 16, 18,
#ifdef EMULE_HAVE_BITTORRENT
    19,
#endif
#ifdef EMULE_HAVE_HOSTER_JD
    24,
#endif
    17, 1, 23, 20, 21, 22
};

[[nodiscard]] inline const ToolbarButtonDef* findButtonDef(ToolbarButtonId id)
{
    for (const auto& def : kAllButtons) {
        if (def.id == id)
            return &def;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Skin tiles
// ---------------------------------------------------------------------------

/// Position of an icon in a toolbar bitmap strip (`*.eMuleToolbar.kad02.bmp`).
///
/// The first 14 are MFC's layout (srchybrid/MuleToolBarCtrl.cpp:548, NUM_BUTTON_BITMAPS)
/// and may never move. Ours are appended and optional: a strip may end anywhere from
/// Help on, and the missing tiles fall back to the profile or built-in icons.
enum class ToolbarTile : int {
    Connect, Disconnect, StopConnecting,
    Kad, Servers, Transfers, Search, SharedFiles, Messages, IRC, Statistics,
    Options, Tools, Help,
    // eMule Qt only
    Usenet, Torrents, Hoster, DownloadsFolder,
};

struct ToolbarTileDef {
    ToolbarTile tile;
    ToolbarButtonId button;
    const char* skinKey; ///< [Icons] key in *.eMuleSkin.ini (MFC resource name)
};

/// Strip order. Official eMule refuses a strip wider than 14 tiles, so a skin made
/// for both clients puts our icons in the profile's [Icons] section instead.
inline constexpr ToolbarTileDef kToolbarTiles[] = {
    {ToolbarTile::Connect,         ToolbarButtonId::Connect,         "CONNECT"},
    {ToolbarTile::Disconnect,      ToolbarButtonId::Connect,         "DISCONNECT"},
    {ToolbarTile::StopConnecting,  ToolbarButtonId::Connect,         "STOPCONNECTING"},
    {ToolbarTile::Kad,             ToolbarButtonId::Kad,             "KADEMLIA"},
    {ToolbarTile::Servers,         ToolbarButtonId::Servers,         "SERVER"},
    {ToolbarTile::Transfers,       ToolbarButtonId::Transfers,       "TRANSFER"},
    {ToolbarTile::Search,          ToolbarButtonId::Search,          "SEARCH"},
    {ToolbarTile::SharedFiles,     ToolbarButtonId::SharedFiles,     "SharedFiles"},
    {ToolbarTile::Messages,        ToolbarButtonId::Messages,        "MESSAGES"},
    {ToolbarTile::IRC,             ToolbarButtonId::IRC,             "IRC"},
    {ToolbarTile::Statistics,      ToolbarButtonId::Statistics,      "STATISTICS"},
    {ToolbarTile::Options,         ToolbarButtonId::Options,         "PREFERENCES"},
    {ToolbarTile::Tools,           ToolbarButtonId::Tools,           "TOOLS"},
    {ToolbarTile::Help,            ToolbarButtonId::Help,            "HELP"},
    {ToolbarTile::Usenet,          ToolbarButtonId::Usenet,          "USENET"},
    {ToolbarTile::Torrents,        ToolbarButtonId::Torrents,        "TORRENT"},
    {ToolbarTile::Hoster,          ToolbarButtonId::Hoster,          "HOSTER"},
    {ToolbarTile::DownloadsFolder, ToolbarButtonId::DownloadsFolder, "DOWNLOADSFOLDER"},
};

inline constexpr int kOfficialToolbarTiles = 14;
inline constexpr int kToolbarTileCount = static_cast<int>(std::size(kToolbarTiles));

static_assert([] {
    for (int i = 0; i < kToolbarTileCount; ++i) {
        if (static_cast<int>(kToolbarTiles[i].tile) != i)
            return false;
    }
    return true;
}(), "kToolbarTiles must be indexed by ToolbarTile");

/// Tile a button shows; Connect gets its disconnected state (the caller swaps it).
/// The first match wins, which for Connect is ToolbarTile::Connect.
[[nodiscard]] inline std::optional<ToolbarTile> tileForButton(ToolbarButtonId id)
{
    for (const auto& def : kToolbarTiles) {
        if (def.button == id)
            return def.tile;
    }
    return std::nullopt;
}

} // namespace eMule

# Import from eMule / aMule

First page of the first start wizard (also Tools > eMule First Runtime Wizard). Replaced the
Windows installer's "Import settings from eMule" task (report 36: users looked for it in the app).

## Flow

1. GUI `LegacyImportPage` asks the core: IPC `FindLegacyProfiles` (225). The core searches its
   own host, so a remote core lists its own installs. Browse buttons only for a local core.
2. User picks an install (combo when more than one), can edit the settings folder and the
   download (temp) folders (`;`-separated). Editing the settings folder re-probes it
   (`FindLegacyProfiles [dir]`) and refills the download folders unless the user typed them.
3. Next imports settings only: `ImportLegacyProfile` (226) `[dir, true, false, []]`. Core writes
   `<config>/mfc-import.yml` and restarts itself (`DaemonApp::requestRestart`, same args, so the
   first-start connect hold stays). On start `LegacyConfigImport::run()` imports before anything
   reads the config dir. The wizard waits for `IpcClient::connected`, re-reads the prefs, shows the
   ports page with imported values (one "TCP/UDP" field, see below).
4. Downloads wait for Finish: `LegacyImportPage::importDownloads()` sends
   `[dir, false, true, tempDirs]`, fire and forget, outcome in the status bar. Core lists the
   folders off its thread and queues them with `PartFileConvert`; the convert thread reads the old
   .part.met itself, only the parse + queueing runs on the core main thread (`PartMetPreload`).
   Cancel sends nothing, the old client keeps its downloads. (A request file with parts from an
   older GUI still works: `takePendingTempDirs()` at start.)
   Settings done once per wizard run: Back + Next does not repeat it, settings inputs stay locked;
   download inputs stay editable until Finish.
5. Nothing auto-detected (or no core to ask): the page is left out, the wizard starts on the
   ports page and Back stops there (`LegacyImportPage::searchFinished(false)`).

## Discovery (`LegacyConfigImport::discover`)

| OS | eMule (preferences.ini) | aMule (amule.conf) |
|---|---|---|
| Windows | `%LOCALAPPDATA%\eMule\config`, `%PROGRAMDATA%\eMule\config`, exe dir of the HKCU ed2k / eMule handler and `Run\eMuleAutoStart`, `Program Files*\eMule\config`, `%APPDATA%\eMule\config` (XP) | `%APPDATA%\aMule` |
| macOS | Wine / CrossOver bottles | `~/Library/Application Support/aMule`, `~/.aMule` |
| Linux | Wine (`$WINEPREFIX`, `~/.wine`, `~/.cxoffice/*`) | `~/.aMule`, Flatpak `~/.var/app/org.amule.aMule/{data/,}.aMule` |

Wine: `C:\…` paths in the ini map through `<prefix>/dosdevices/<x>:`. Same order as the old
installer (eMule's `GetDefaultDirectory`).

## What is imported

- data files: preferences.dat + cryptkey.dat (only as a pair), clients.met, known.met,
  known2_64.met, cancelled.met, emfriends.met, server.met, staticservers.dat, addresses.dat,
  nodes.dat, preferencesKad.dat, ipfilter.dat, fileinfo.ini, webservices.dat. Our own copies
  go to `<config>/import-backup-<date>/` first (moved).
- settings backup: preferences.yml, uistate.yml (GUI layout, when it shares the config dir) and
  preferences.ini (scheduler) are copied to the same folder before anything is changed.
- port: the TCP port becomes the one port for eD2K TCP, eD2K UDP and ed3k UDP (eD2K compat,
  ed3k Protocol.md §14.1); the old UDP port and aMule's `UDPEnable=0` are dropped.
- user hash: preferences.dat + cryptkey.dat in the source → taken over (`LegacyIdentityImport`,
  credits). Neither pair there → ours becomes the ed3k-derived hash (§14.2), old one logged.
- prefs: nick (not the clients' homepage default), capacities,
  limits, max connections / sources, autoconnect, reconnect, Kad / eD2K, incoming dir.
  Encryption never (must stay on).
- categories (only into an untouched list): eMule Category.ini, aMule `[Cat#1..N]`.
- shared dirs: shareddir.dat (eMule UTF-16/ANSI, aMule UTF-8).
- eMule scheduler sections → `<config>/preferences.ini`.
- unfinished downloads: moved into our temp dir (`PartFileConvert`, `removeSource`): no disk
  doubled, and the old client cannot run the same download. The .part.met is copied first and the
  sources are removed only once the download loaded; a failed one gets its .part back.

aMule's amule.conf is wxFileConfig: UTF-8, `\\` `\"` `\n` escapes, values quoted for outer blanks.

Tests: `tests/tst_LegacyConfigImport.cpp`.

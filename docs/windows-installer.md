# Windows installer

`eMuleQt-v<version>-win64-setup.exe` is built by the Windows CI next to the portable zip, from
the same staged files. It is an [Inno Setup](https://jrsoftware.org/isinfo.php) installer:
`installer/windows/emuleqt.iss`, compiled by `scripts/installer-win.ps1`.

## What setup asks

Two pages, then it installs:

1. **Destination folder** — default `C:\Program Files\eMule Qt`. Skipped when upgrading.
2. **Three checkboxes**
   - Create a desktop shortcut
   - Create a Start Menu shortcut
   - Import settings from eMule — ticked when an eMule configuration was found (its folder is
     shown), greyed out when none was. On an upgrade of eMule Qt it is offered but not ticked.

Setup needs administrator rights (it writes to Program Files). It is not code-signed, so
Windows SmartScreen warns on first run.

Silent install: `setup.exe /VERYSILENT /SUPPRESSMSGBOXES /DIR="..." /TASKS="desktopicon,startmenuicon,importemule"`.
Silent runs never import unless `importemule` is named.

## Where things go

| What | Where |
|---|---|
| Program, Qt runtime, default data | install folder |
| Settings, credits, known files, server list | `%LOCALAPPDATA%\eMuleQt` |
| Incoming, Temp (defaults) | `%LOCALAPPDATA%\eMuleQt\Incoming`, `...\Temp` |

The zip keeps its configuration next to the exe (portable). Under Program Files that folder is
read-only, so setup installs `config\preferences.yml` with `transfer.multiUserSharing: 0`, the
one key the app reads from there to pick the per-user location. That file and the import
request are never copied into the user's configuration (`AppConfig`, `kNeverSeed`).

## How an existing eMule is found

Setup looks for `preferences.ini` the way eMule itself picks its configuration folder
(`srchybrid/Preferences.cpp`, `GetDefaultDirectory`):

1. `HKCU\Software\eMule\UsePublicUserDirectories`, when set: 0 = per user, 1 = all users,
   2 = program folder — that location is tried first.
2. `%LOCALAPPDATA%\eMule\config`
3. `%ProgramData%\eMule\config`
4. `config\` next to `emule.exe`. eMule stores no install path, so the exe is taken from what it
   registers: the `ed2k://` handler, the collection file type, the autostart entry — then
   `Program Files (x86)\eMule` and `Program Files\eMule`.
5. `%APPDATA%\eMule\config` (Windows XP layout of old installs).

Other clients that registered `ed2k://` are ignored: the program must be `emule.exe`.

## What the import does

Setup runs elevated and only writes a request, `config\mfc-import.yml`, into the install
folder. The daemon carries it out on its next start, as the user who runs it, **once per
Windows user** (`src/core/prefs/LegacyConfigImport`). The old eMule installation is only read
and keeps working.

| Imported | From |
|---|---|
| User hash and key (credits at other clients are kept) | `preferences.dat`, `cryptkey.dat` |
| Credits, known files, AICH hashes, cancelled files | `clients.met`, `known.met`, `known2_64.met`, `cancelled.met` |
| Friends, servers, static servers, Kad nodes and Kad ID | `emfriends.met`, `server.met`, `staticservers.dat`, `addresses.dat`, `nodes.dat`, `preferencesKad.dat` |
| IP filter, file comments, web services | `ipfilter.dat`, `fileinfo.ini`, `webservices.dat` |
| Nick, TCP/UDP port, speed limits and capacities, max connections, max sources per file, autoconnect, reconnect, eD2K/Kad on or off, incoming folder | `preferences.ini` |
| Scheduler entries | `preferences.ini` (`[Scheduler]`, `[Schedule#N]`) |
| Categories | `Category.ini` |
| Shared folders (those that still exist) | `shareddir.dat` |
| Unfinished downloads | old temp folder(s), **copied** through the part-file converter |

Not imported:

- **Encryption settings.** Obfuscation stays as eMule Qt has it; most peers require it.
- Statistics, window layout, language, proxy, web-server and other passwords.
- The old temp folder itself. Two clients working on the same part files corrupt them, so the
  downloads are copied into the new temp folder. That needs the disk space a second time; the
  progress is in *Tools > Import Downloads*.

Close eMule before the first start of eMule Qt: its part files are copied while they are read,
and both clients would otherwise listen on the same imported ports.

The import is skipped, and never tried again, when the eMule Qt profile is already in use
(it has a `known.met` or `clients.met`) or when the recorded folder has no `preferences.ini`
for this user. The outcome is in the log. To repeat it, delete `mfc-import.done` from
`%LOCALAPPDATA%\eMuleQt` (and the files listed above, or it counts as in use).

If the UAC prompt is answered with a different administrator account, setup looks at that
account's profile. The per-user locations are written to the request as `%LOCALAPPDATA%\...`,
so the import still finds the right folder when the normal user starts eMule Qt.

## Uninstall

Removes the install folder, the shortcuts, the `.nzb` association, the `ed2k://` handler and
the autostart entry when they point at this installation. `%LOCALAPPDATA%\eMuleQt` — settings
and downloads — is kept.

## Building it locally

```powershell
scripts\bundle-win.ps1          # stages stage\eMule and writes the zip
scripts\installer-win.ps1       # build\eMuleQt-v<version>-win64-setup.exe
```

Needs Inno Setup 6.3 or later; `-InstallInno` installs it with Chocolatey, `-Iscc <path>` names
the compiler explicitly.

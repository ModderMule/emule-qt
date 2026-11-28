# Portable mode

A portable copy keeps config + Incoming/Temp in its own folder and runs its own core,
like a `--config` profile. Several copies run side by side, each GUI starting the
`emulecored` next to it.

## Enabling

| OS | Portable when | Data root | Config |
|---|---|---|---|
| Windows | default (`multiUserSharing: 2`, release zip) | `<exe dir>` | `<exe dir>/config` |
| macOS | `eMuleQt/` dir next to `eMule Qt.app` | `<app dir>/eMuleQt` | `.../eMuleQt/Config` |
| Linux | `eMuleQt/` dir next to the `.AppImage`, or next to `emuleqt` (tarball) | `<app dir>/eMuleQt` | `.../eMuleQt/Config` |

Without that dir macOS/Linux use the home default `~/eMuleQt`. To move an existing setup,
move `~/eMuleQt` into the app folder. A dir equal to `~/eMuleQt` never counts as portable.

## Behaviour

- `AppConfig::isPortableProfile()`: portable and no `--config`.
- No per-user core lock (`emulecored-user.lock`): a portable core never blocks or attaches
  to another install's core. Setup (per-user config) installs keep one core per user.
- Single GUI instance keyed by exe dir + config dir, not per user.
- Configured core mode (`ipcDaemonPath`: path or `local`) and remote IPC address are kept.
- Ports: a taken IPC port moves to a free one; TCP/UDP/BitTorrent move only on first start
  (wizard not done). Configured ports are kept: a copied, configured folder needs its
  ports changed by hand before both run.
- Updates overlay program files only; the portable data dir is never touched.
- File types / ed2k links: the last started copy registers itself.

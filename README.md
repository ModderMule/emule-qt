# eMule Qt

[![Windows CI](https://github.com/ModderMule/emule-qt/actions/workflows/windows.yml/badge.svg)](https://github.com/ModderMule/emule-qt/actions/workflows/windows.yml) [![macOS CI](https://github.com/ModderMule/emule-qt/actions/workflows/macos.yml/badge.svg)](https://github.com/ModderMule/emule-qt/actions/workflows/macos.yml) [![Linux CI](https://github.com/ModderMule/emule-qt/actions/workflows/linux.yml/badge.svg)](https://github.com/ModderMule/emule-qt/actions/workflows/linux.yml)

**eMule Qt** is a free, open-source peer-to-peer file sharing client for the **ED2K** and **Kademlia** networks — rebuilt from the ground up with **Qt 6** and **modern C++23**.

Cross-platform. Fully compatible with the existing eMule network.

Website: [emule-qt.org](https://emule-qt.org/)

## Features

- **ED2K & Kademlia** — Server-based and decentralized DHT networks for maximum source availability
- **Protocol Obfuscation** — TCP/UDP encryption enabled by default
- **AICH Verification** — Chunk-level integrity checks with automatic corruption recovery
- **Queue & Credit System** — The proven eMule reward system for fair sharing
- **Built-in Web Server** — Remote control from any browser via the classic web interface
- **Advanced Search** — Expression-based queries across multiple networks
- **IRC Chat & Messages** — Built-in IRC client and peer-to-peer messaging
- **IP Filtering** — Dynamic blocklists with automatic updates
- **Media & Archive Preview** — Preview video files and browse archives before download completes
- **Statistics** — Real-time bandwidth graphs, connection metrics, and Kademlia visualization
- **Internationalization** — 9 languages out of the box

## Unique Features

*Not found in the original eMule.*

- **Cross-Platform** — One codebase running natively on Windows, macOS, and Linux; the original is MFC and Windows-only
- **Daemon/GUI Split** — Headless `emulecored` + `emuleqt` GUI over an encrypted CBOR IPC link; run the core on a server or NAS and attach the GUI locally or remotely
- **IPv6** — Dual-stack across client-to-client, client-to-server, Kademlia, and eD2K links, so IPv6-only and CGNAT users can participate fully — additive and legacy-safe ([spec](docs/protocol/ipv6-spec.md))
- **HTTP Cache** — When several peers want the same part, encrypt and upload it once, then hand each peer a URL and key; the cache server never sees plaintext ([spec](docs/protocol/http-cache-spec.md))
- **PCP & NAT-PMP Port Mapping** — Three backends raced at startup — PCP, NAT-PMP, and UPnP IGD1/IGD2 with IPv6 pinholes — graded on whether the exact port was actually granted ([details](docs/port-mapping.md))
- **REST API** — A JSON `/api/v1/*` interface with API-key auth alongside the classic web UI, for scripts and dashboards
- **CLI Control** — Drive a running daemon from the shell: `--add-link`, `--connect`, `--disconnect`, `--connect-kad`
- **Extended Source Exchange** — Source exchange widened with each source's server endpoint, user hash, and obfuscation options, negotiated per peer
- **Save/Load Sources** — Each part file's best sources are written to disk and re-injected on restart, so a download resumes at speed instead of rebuilding its source list
- **Upload Queue Persistence** — The waiting list survives a restart with every peer's earned wait time intact, so queued peers keep their position ([details](docs/upload-queue-storage.md))
- **Hardened Kademlia** — Node reputation with one-node-per-IP enforcement, variance-based adaptive timeouts, and a public IP adopted only once several independent peers agree on it
- **Persistent Statistics** — Graph history is collected in the core rather than the window, so traces survive a GUI restart and two GUIs on one daemon see the same picture
- **Modern Foundation** — C++23 and Qt 6 throughout, human-readable YAML configuration with encrypted secrets, a cross-platform crash handler, and 150+ automated test executables

## Building

**Requirements:**
- Qt 6.8+
- CMake 3.25+
- C++23 compiler (Clang 16+, GCC 13+, MSVC 2022+)

### macOS / Linux

```bash
scripts/build.sh
```

Binaries are output to `build/`.

### Windows

Open `src/eMuleQt.sln` in Visual Studio 2022 (or later) and build. See [docs/windows-build.md](docs/windows-build.md) for dependency setup (should be done by Visual Studio automatically).

## Running

Start the GUI (launches the daemon automatically):
```bash
build/emuleqt
```

Optionally run the daemon standalone for headless/server use — the GUI can connect remotely over IPC (default port 4712):
```bash
build/emulecored
```

To create a distributable app bundle, use the platform-specific scripts: `scripts/bundle-macos.sh`, `scripts/bundle-linux.sh`, or `scripts/bundle-win.ps1`.

## Architecture

```
emuleqt (GUI)  ←—  IPC/CBOR over TCP  —→  emulecored (Daemon)
                                              ├── ED2K Protocol
                                              ├── Kademlia DHT
                                              ├── Web Server / REST API
                                              └── File Management
```

## Contributing

- Report bugs and suggest features via [GitHub Issues](https://github.com/ModderMule/emule-qt/issues)
- Submit pull requests
- Add translations using Qt Linguist (see `lang/`)

## License

GPL-2.0 — see [LICENSE](LICENSE).

Based on the original [eMule](https://www.emule-project.com/) by Merkur.
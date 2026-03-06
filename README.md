# eMule Qt

**eMule Qt** is a free, open-source peer-to-peer file sharing client for the **ED2K** and **Kademlia** networks — rebuilt from the ground up with **Qt 6** and **modern C++23**.

Cross-platform. Fully compatible with the existing eMule network.

Website: [emule-qt.org](https://emule-qt.org/)

## Features

- **ED2K & Kademlia** — Server-based and decentralized DHT networks for maximum source availability
- **Cross-Platform** — Runs natively on Windows, macOS, and Linux
- **Daemon/GUI Split** — Headless daemon (`emulecored`) + Qt GUI (`emuleqt`), connect remotely or run on a server
- **Protocol Obfuscation** — TCP/UDP encryption enabled by default
- **AICH Verification** — Chunk-level integrity checks with automatic corruption recovery
- **Queue & Credit System** — The proven eMule reward system for fair sharing
- **Built-in Web Server** — Remote control via web interface and REST API
- **Advanced Search** — Expression-based queries across multiple networks
- **IRC Chat & Messages** — Built-in IRC client and peer-to-peer messaging
- **IP Filtering** — Dynamic blocklists with automatic updates
- **Media & Archive Preview** — Preview video files and browse archives before download completes
- **Statistics** — Real-time bandwidth graphs, connection metrics, and Kademlia visualization
- **Internationalization** — 9 languages out of the box

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

Open `src/eMuleQt.sln` in Visual Studio 2022 and build. See [docs/windows-build.md](docs/windows-build.md) for dependency setup (should be done by Visual Studio automatically).

## Running

Start the daemon:
```bash
build/emulecored
```

Start the GUI (connects to the daemon automatically):
```bash
build/emuleqt
```

The daemon can also run headless on a server — connect the GUI remotely over IPC (default port 4712).

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
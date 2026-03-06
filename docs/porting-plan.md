# eMule MFC-to-Qt6 Porting Plan

## Overview

Port the eMule filesharing client (~465 source files, ~250 modules) from Microsoft Foundation Classes (MFC) / Win32 to Qt 6 with modern C++23, targeting cross-platform support (Windows, macOS, Linux).

**Architecture:** Daemon/GUI split — a headless `emulecored` daemon runs the core P2P engine, while the `emuleqt` GUI connects via a CBOR-over-TCP IPC protocol. Both link against `libemulecore` (static library) and `libemuleipc` (shared IPC protocol library).

**Source directory (original MFC):** `srchybrid/`
**Ported source directory:** `src/` — `core/`, `ipc/`, `daemon/`, `gui/`
**Kademlia subsystem:** `src/core/kademlia/` (48 files, 6 sub-modules)

---

## Module Breakdown

### Module 1: Build System & Project Scaffolding

Set up the Qt 6 / CMake project structure replacing the Visual Studio `.sln`/`.vcxproj` files.

- [x] Create root `CMakeLists.txt` with Qt 6 discovery (`find_package(Qt6 REQUIRED)`) — `CMakeLists.txt`
- [x] Define project-wide C++23 standard (`set(CMAKE_CXX_STANDARD 23)`) — `cmake/CompilerSettings.cmake`
- [x] Create modular `CMakeLists.txt` per library/module — `src/CMakeLists.txt`, `src/core/CMakeLists.txt` (17 sub-modules), `src/ipc/CMakeLists.txt`, `src/daemon/CMakeLists.txt`, `src/gui/CMakeLists.txt`, `tests/CMakeLists.txt`
- [x] Set up Qt 6 path hint for `/Users/daniel/Qt` — Qt 6.10.2 in `CMakeLists.txt`
- [x] Replace `Stdafx.h` precompiled header with CMake PCH support — `target_precompile_headers()` on `emulecore` with 15 C++ stdlib + 8 Qt Core headers; GUI PCH deferred until more source files exist
- [x] Create `config.h.in` / platform abstraction header replacing `emule_site_config.h` — `src/core/config.h.in`
- [x] Set up `.gitignore` for build artifacts — `.gitignore`
- [x] Verify clean build of empty skeleton on macOS — build 100%, `ctest` 1/1 passed (Qt 6.10.2, AppleClang 17, C++23)

---

### Module 2: Platform Abstraction Layer

Replace Windows-only types, macros, and APIs with cross-platform equivalents.

- [x] Replace `DWORD`, `UINT`, `BOOL`, `BYTE`, `WORD`, `LONG`, `LPCTSTR`, etc. with `<cstdint>` types — `utils/Types.h` (extended) + `utils/WinCompat.h`
- [x] Replace `TCHAR` / `CString` with `QString` — `utils/StringUtils.h/.cpp` (`EMUSTR()`, `fromStdString()`, `toStdString()`)
- [x] Replace `ASSERT` / `VERIFY` with `Q_ASSERT` — `utils/DebugUtils.h` (`EMULE_ASSERT`, `EMULE_VERIFY`, `EMULE_ASSERT_VALID`)
- [x] Replace `TRACE` with `qDebug()` / `qWarning()` / `qCritical()` — `utils/DebugUtils.h/.cpp` (6 `Q_LOGGING_CATEGORY`: general, net, file, kad, server, crypto)
- [x] Replace `SYSTEMTIME`, `FILETIME` with `QDateTime` — `utils/TimeUtils.h` (`fileTimeToUnixTime()`, `unixTimeToFileTime()`, `toDateTime()`, `fileTimeToDateTime()`, `dateTimeToFileTime()`)
- [x] Replace `CriticalSection` / `HANDLE`-based threading with `QMutex`, `QThread`, `std::mutex` — `utils/ThreadUtils.h` (`Mutex`, `Lock`, `SharedMutex`, `ReadLock`, `WriteLock`)
- [x] Replace `CEvent` with `QWaitCondition` or `std::condition_variable` — `utils/ThreadUtils.h` (`ManualResetEvent`, `AutoResetEvent`)
- [x] Replace `CWinThread` with `QThread` — documented migration to `std::jthread` in `utils/ThreadUtils.h`
- [x] Replace `InterlockedIncrement` / `InterlockedDecrement` with `std::atomic` — `utils/ThreadUtils.h` (`Atomic<T>`)
- [x] Replace ATL collections (`CAtlMap`, `CAtlList`, `CAtlArray`) with STL containers — `utils/ContainerUtils.h` (migration docs + `sortAscending`, `sortBy`, `binaryFind`, `eraseIf`)
- [x] Replace `MFC CArray`, `CList`, `CMap` with STL containers — `utils/ContainerUtils.h` (detailed mapping table)
- [x] Create `Types.h` compatibility header with cross-platform typedefs — `utils/Types.h` (uint8–64, int8–64, sint8–64, uchar, EMFileSize, usize, isize)
- [x] Replace `_T()` / `TEXT()` macros (remove or map to `QStringLiteral`) — `utils/StringUtils.h` (`EMUSTR(s)` → `QStringLiteral(s)`)
- [x] Replace Windows `Sleep()` with `QThread::msleep()` or `std::this_thread::sleep_for` — `utils/TimeUtils.h` (`sleepMs()`, `sleep()`)
- [x] Replace `GetTickCount()` / `GetTickCount64()` with `QElapsedTimer` or `std::chrono` — `utils/TimeUtils.h` (`getTickCount()`, `now()`, `elapsedMs()`, `HighResTimer`)
- [x] Replace Windows registry access with `QSettings` — `utils/SettingsUtils.h/.cpp` (`Settings` class, INI format)
- [x] Replace Windows path APIs (`GetModuleFileName`, `SHGetFolderPath`) with `QStandardPaths` — `utils/PathUtils.h/.cpp` (`AppDir`, `appDirectory()`, `executablePath()`, etc.)
- [x] Create umbrella header — `utils/PlatformUtils.h` (single include for all platform utils)

---

### Module 3: Core Utility Functions (`OtherFunctions`, `StringConversion`, helpers)

Port the shared utility layer that most other modules depend on.

- [x] Port `OtherFunctions.cpp/h` — `utils/OtherFunctions.h/.cpp` (MD4 helpers, base16/32, URL encode/decode, IP helpers, RC4, file types, peek/poke, string helpers, Levenshtein distance)
- [x] Port `StringConversion.cpp/h` — covered by Module 2's `utils/StringUtils.h/.cpp`
- [x] Port `Ini2.cpp/h` — covered by Module 2's `utils/SettingsUtils.h/.cpp`
- [x] Port `SafeFile.cpp/h` — `utils/SafeFile.h/.cpp` (FileDataIO, SafeFile, SafeMemFile with typed read/write)
- [x] Port `Parser.cpp/h` — `search/SearchExprParser.h/.cpp` (hand-written recursive descent parser replacing Bison/Yacc, supports all attribute filters, boolean operators, quoted strings, ED2K links)
- [x] Port `MapKey.cpp/h` — `utils/MapKey.h` (HashKeyRef, HashKeyOwn with std::hash specializations)
- [x] Port `TimeTick.cpp/h` — covered by Module 2's `utils/TimeUtils.h` (HighResTimer)
- [x] Port `DebugHelpers.cpp/h` — covered by Module 2's `utils/DebugUtils.h`
- [x] Port `Exceptions.cpp/h` — `utils/Exceptions.h` (EmuleException, MsgBoxException, ClientException, IOException, ProtocolException, EMULE_CATCH_ALL macro)
- [x] Port `Log.cpp/h` — `utils/Log.h/.cpp` (LogFile with rotation, logInfo/logWarning/logError/logDebug convenience functions)
- [x] Port `PerfLog.cpp/h` — `utils/PerfLog.h/.cpp` (CSV/MRTG format performance logging)
- [x] Port `MenuCmds.cpp/h` — deferred to GUI modules (only menu command ID constants)
- [x] Port `Types.h` / `Opcodes.h` / `Packets.h` — Types.h in Module 2, `utils/Opcodes.h` (all protocol constants, portable), Packets.h deferred to Module 7

---

### Module 4: Cryptography & Hashing

Port hash functions; mostly pure C++ with minimal platform dependencies.

- [x] Port `MD4.cpp/h` → `crypto/MD4Hash.h/.cpp` — wraps `QCryptographicHash::Md4`
- [x] Port `MD5Sum.cpp/h` → `crypto/MD5Hash.h/.cpp` — wraps `QCryptographicHash::Md5`
- [x] Port `SHA.cpp/h` → `crypto/SHAHash.h/.cpp` — wraps `QCryptographicHash::Sha1`, implements `AICHHashAlgo`
- [x] Port `SHAHashSet.cpp/h` → `crypto/AICHData.h/.cpp`, `crypto/AICHHashTree.h/.cpp`, `crypto/AICHHashSet.h/.cpp`
- [x] Port `AICHSyncThread.cpp/h` → `crypto/AICHSyncThread.h/.cpp` — QThread subclass, loads known2_64.met index, syncs shared files, queues AICH hashing
- [x] Port `FileIdentifier.cpp/h` → `crypto/FileIdentifier.h/.cpp`
- [x] Replaced CryptoPP with Qt 6 `QCryptographicHash` (zero external dependencies)
- [ ] Port `CaptchaGenerator.cpp/h` — deferred (needs Qt6::Gui)

---

### Module 5: Networking & Socket Layer

Replace MFC/Winsock sockets with `QTcpSocket` / `QUdpSocket` / `QAbstractSocket`.

- [x] Port `AsyncSocketEx.cpp/h` — eliminated; Qt's `QAbstractSocket` provides async I/O natively
- [x] Port `AsyncSocketExLayer.cpp/h` — eliminated; layered architecture replaced by Qt signals/slots
- [x] Port `AsyncProxySocketLayer.cpp/h` — replaced with `QNetworkProxy` in `EMSocket::initProxySupport()`
- [x] Port `EMSocket.cpp/h` — `net/EMSocket.h/.cpp`, packet framing + queuing over `QTcpSocket`
- [x] Port `ThrottledSocket.cpp/h` — `net/ThrottledSocket.h` (header-only interfaces)
- [x] Port `EncryptedStreamSocket.cpp/h` — `net/EncryptedStreamSocket.h/.cpp`, RC4 obfuscation (not TLS)
- [x] Port `EncryptedDatagramSocket.cpp/h` — `net/EncryptedDatagramSocket.h/.cpp`, encrypted UDP
- [x] Port `Packets.cpp/h` (basic Packet container) — `net/Packet.h/.cpp` (protocol-specific packets in Module 7)
- [x] Port `Pinger.cpp/h` — `net/Pinger.h/.cpp`, cross-platform ICMP/UDP ping via POSIX sockets (Qt has no raw ICMP API)
- [x] ~~Port `TLSthreading.cpp/h`~~ — **not needed**; Qt handles TLS threading internally via `QSslSocket`/OpenSSL
- [x] ~~Replace all `WSA*` calls~~ — eliminated by Qt socket migration; remaining `WSA*`/`closesocket()`/`ioctlsocket()` calls are in deferred files and will be removed when each file is ported
- [x] Port `ClientUDPSocket.cpp/h` — `net/ClientUDPSocket.h/.cpp`, `QUdpSocket` + EncryptedDatagramSocket + ThrottledControlSocket, packet queue with expiry
- [x] Port `UDPSocket.cpp/h` — `net/UDPSocket.h/.cpp`, `QUdpSocket` + `QDnsLookup` (replaces MFC CWnd DNS messages), signal-based decoupling from ServerConnect
- [x] Port `ListenSocket.cpp/h` — `net/ListenSocket.h/.cpp` (`QTcpServer`) + `net/ClientReqSocket.h/.cpp` (EMSocket subclass), split into two class pairs
- [x] Port `ServerSocket.cpp/h` — `net/ServerSocket.h/.cpp`, extends EMSocket + server protocol, `QDnsLookup` for dynIP, Qt signals replace `friend class CServerConnect`
- [x] Port `HttpClientReqSocket.cpp/h` — `net/HttpClientReqSocket.h/.cpp`, EMSocket raw data mode + HTTP state machine, includes HttpClientDownSocket subclass
- [x] Port `WebSocket.cpp/h` — subsumed by REST API in Module 19 (`QHttpServer` replaces mbedtls + raw SOCKET)
- [x] Port `LastCommonRouteFinder.cpp/h` — `net/LastCommonRouteFinder.h/.cpp`, `QThread` + `std::mutex` + `std::condition_variable` + Pinger, signal-based host collection
- ~~Port `HttpDownloadDlg.cpp/h`~~ — moved to GUI module (Module 22+): `QNetworkAccessManager` + `QProgressDialog`

---

### Module 6: Firewall & UPnP

- [x] ~~Port `FirewallOpener.cpp/h`~~ — removed (Windows ICS/COM, no cross-platform equivalent)
- [x] ~~Port `UPnPImpl.cpp/h`~~ — replaced by unified `UPnPManager`
- [x] Port `UPnPImplMiniLib.cpp/h` — miniupnpc → `upnp/UPnPManager.cpp`
- [x] ~~Remove `UPnPImplWinServ.cpp/h`~~ — removed (Windows-only IUPnPNAT COM interface)
- [x] ~~Port `UPnPImplWrapper.cpp/h`~~ — removed (single implementation, no factory needed)
- [x] Integrate miniupnpc as external dependency via CMake `FetchContent`

---

### Module 7: ED2K Protocol & Packet Handling

Core protocol logic — mostly platform-independent, needs socket layer porting.

- [x] Port `Opcodes.h` (protocol constants — should be largely portable) — `utils/Opcodes.h` (676 lines, all constants preserved)
- [x] Port `Packets.cpp/h` (basic Packet container done in Module 5) — `net/Packet.h/.cpp`
- [x] Port `Tags.cpp/h` (protocol tag system) — `protocol/Tag.h/.cpp` (CTag → Tag with `std::variant` storage, old + new ED2K format serialization)
- [x] Port `ED2KLink.cpp/h` (ed2k:// URL parsing) — `protocol/ED2KLink.h/.cpp` (5 link types + magnet links, `std::optional` return instead of exceptions)
- [ ] Port `ED2kLinkDlg.cpp/h` → Qt dialog — deferred to GUI module
- [x] Verify endianness handling is correct for non-x86 platforms — SafeFile uses `memcpy`-based peek/poke, portable across architectures

---

### Module 8: Server Management

- [x] Port `Server.cpp/h` (server entity class)
- [x] Port `ServerList.cpp/h` (server list persistence and management)
- [x] Port `ServerConnect.cpp/h` (server connection state machine) — `server/ServerConnect.h/.cpp`, state machine with multi-server connect, timeout, retry, Qt signals
- [ ] Port `WebServices.cpp/h` *(deferred: GUI module)*

---

### Module 9: Client Management & Credit System

Port the large client management subsystem.

- [x] Port `AbstractFile.cpp/h` (base file representation)
- [x] Port `UpdownClient.h` (full class declaration, ~180 members, Phase 1 methods)
- [x] Port `BaseClient.cpp` Phase 1 (identity, state, Compare, version detection, debug strings)
- [x] Port `BaseClient.cpp` Phase 2 (hello handshake, mule info exchange, connection mgmt)
- [x] Port `BaseClient.cpp` Phase 3 (secure identity, chat, preview, firewall check, misc protocol)
- [x] Port `UploadClient.cpp/h` (scoring, block management, upload statistics)
- [x] Port `DownloadClient.cpp/h` (file requests, block transfer, source swapping — integrated with PartFile)
- [x] Port `URLClient.cpp/h` (HTTP download client subclass)
- [x] Port `ClientList.cpp/h` (add/remove/find, banned IP tracking, DeadSourceList)
- [x] Port `ClientCredits.cpp/h` (credit/reputation system — RSA crypto deferred)
- [x] Port `ClientStateDefs.h`
- [x] Port `DeadSourceList.cpp/h`
- [x] Port `CorruptionBlackBox.cpp/h`

---

### Module 10: Kademlia / DHT

Port the full Kademlia distributed hash table implementation (48 files).

#### 10a: Kademlia I/O Layer (`kademlia/io/`)
- [x] Port `ByteIO.cpp/h` — replaced by `KadIO` free functions operating on `FileDataIO`
- [x] Port `DataIO.cpp/h` — replaced by `KadIO` free functions
- [x] Port `FileIO.cpp/h` — replaced by `SafeFile` / `SafeMemFile`
- [x] Port `BufferedFileIO.cpp/h` — replaced by `SafeMemFile`
- [x] Port `IOException.cpp/h` — replaced by `FileException` in `SafeFile.h`

#### 10b: Kademlia Core (`kademlia/kademlia/`)
- [x] Port `Kademlia.cpp/h` → `Kademlia.cpp/h` (static instance + engine wiring)
- [x] Port `Prefs.cpp/h` → `KadPrefs.cpp/h` (Phase 2)
- [x] Port `Search.cpp/h` → `KadSearch.cpp/h` (search state machine + routing integration)
- [x] Port `SearchManager.cpp/h` → `KadSearchManager.cpp/h` (search lifecycle)
- [x] Port `Entry.cpp/h` → `KadEntry.cpp/h` (DHT entries + trust tracking)
- [x] Port `Indexed.cpp/h` → `KadIndexed.cpp/h` (indexed keyword/file/notes store + response building)
- [x] Port `UDPFirewallTester.cpp/h` → `KadFirewallTester.cpp/h`
- [x] Port `Defines.h` → `KadDefines.h` (Phase 1)
- [x] Port `Error.h` / `Tag.h` — `Tag.h` in `protocol/`, errors via Qt exceptions

#### 10c: Kademlia Network (`kademlia/net/`)
- [x] Port `KademliaUDPListener.cpp/h` → `KadUDPListener.cpp/h` (signal-based UDP, all protocol handlers)
- [x] Port `PacketTracking.cpp/h` → `KadPacketTracking.cpp/h`

#### 10d: Kademlia Routing (`kademlia/routing/`)
- [x] Port `Contact.cpp/h` → `KadContact.cpp/h` (Phase 1)
- [x] Port `RoutingBin.cpp/h` → `KadRoutingBin.cpp/h` (Phase 1)
- [x] Port `RoutingZone.cpp/h` → `KadRoutingZone.cpp/h` (Phase 2 + addOrUpdateContact convenience)
- [x] Port `Maps.h` → `KadTypes.h` (Phase 1)

#### 10e: Kademlia Utilities (`kademlia/utils/`)
- [x] Port `UInt128.cpp/h` → `KadUInt128.cpp/h` (Phase 1)
- [x] Port `MiscUtils.cpp/h` → `KadMiscUtils.cpp/h`
- [x] Port `LookupHistory.cpp/h` → `KadLookupHistory.cpp/h`
- [x] Port `ThreadName.cpp/h` — not needed, Qt handles natively via `QThread::setObjectName()`
- [x] Port `KadClientSearcher.h` (Phase 1)
- [x] Port `KadUDPKey.h` (Phase 1)

---

### Module 11: File Management

Phase 1 (complete):
- [x] Port `ShareableFile.cpp/h` — path, shared directory, verified file type
- [x] Port `StatisticFile.cpp/h` — per-file transfer statistics (decoupled from theApp)
- [x] Port `CollectionFile.cpp/h` — collection entry, serialization, ed2k link init
- [x] Port `KnownFile.cpp/h` (partial) — construction, setFileSize, load/write tags, priority, part counts

Phase 2 (complete):
- [x] Port `PartFile.cpp/h` (partial/downloading file) — gap management, buffered I/O, persistence, block selection, status machine
- [x] Port `DownloadQueue.cpp/h` — file management, init from temp dirs, source management

Phase 3 (complete):
- [x] Port `KnownFile.cpp/h` remaining — createFromFile, createHash, createHashFromFile/Memory, updatePartsInfo, publishSrc/Notes stubs
- [x] Port `KnownFileList.cpp/h` — known.met/cancelled.met persistence, hash-based lookup, MD4Key helper
- [x] Port `SharedFileList.cpp/h` — directory scanning, HashingThread, shared file management
- [x] Port `PartFileWriteThread.cpp/h` — `QThread` + `QWaitCondition` for async buffered I/O
- [x] Port `PartFileConvert.cpp/h` — legacy format detection/conversion (old eMule, Shareaza, splitted)
- [x] Port `ArchiveReader.cpp/h` — unified reader via libarchive (replaces ZIPFile, RARFile, GZipFile)
- [x] Port `ArchiveRecovery.cpp/h` — ZIP/RAR recovery from partial downloads

Phase 4 (complete):
- [x] Kad source/notes publishing (`SharedFileList::publish`, `KnownFile::publishSrc/publishNotes`)
- [x] Server list notifications (`SharedFileList::sendListToServer`) — priority-sorted, tag serialization, zlib compression
- [x] ACE/ISO archive recovery — format detection stubs with magic byte identification
- [x] ArchiveRecovery async thread (`recoverAsync` with QThread + auto-delete)
- [x] AICHRecoveryHashSet::saveHashSet/loadHashSet full implementation
- [x] PartFileConvert conversion thread (`ConvertThread` + `performConvertToeMule`)
- [x] Kad `Search::preparePacketForTags` — metadata tag packet builder

Phase 5 (complete):
- [x] Kad keyword publishing — `PublishKeywordList` class with round-robin keyword/file publishing
- [x] Firewall/buddy check in `publishSrc()` — ClientList/UDPFirewallTester integration
- [x] Full format-specific conversion I/O in `performConvertToeMule()` — DefaultOld/Splitted/Shareaza
- [x] ACE/ISO recovery algorithms — sector-based (ISO 9660) and block-based (ACE) recovery
- [x] Hash verification in `PartFile::flushBuffer()` — MD4 part hash verification with gap re-add
- [x] Part availability aggregation in `KnownFile::updatePartsInfo()` — client part-status aggregation
- [x] `updateFileRatingCommentAvail()` in KnownFile, PartFile, CollectionFile — Kad notes cache
- [x] File completion integration — DownloadQueue signal-based SharedFileList/KnownFileList add
- [x] `loadComment()` — QSettings-based filecomments.ini loading
- [x] `createSrcInfoPacket()` — ED2K source info packet builder
- [x] `getWords()` enhancement — min 3 UTF-8 bytes, dedup, extension removal

Phase 6 (complete):
- [x] AICH integration with PartFile — MD4 part hash storage in FileIdentifier during loadPartFile
- [x] AICH tag persistence — FT_AICH_HASH and FT_AICHHASHSET load/save in .part.met
- [x] AICH hash verification in `flushBuffer()` — AICH + MD4 dual verification via `hashSinglePart()`
- [x] AICH master hash verification and hashset save in `completeFile()`
- [x] `requestAICHRecovery()` — find AICH-supporting client, request recovery data
- [x] `aichRecoveryDataAvailable()` — block-level comparison, selective gap filling, MD4 sanity check
- [x] Async file move in `completeFile()` — `FileMoveThread` with cross-filesystem copy fallback
- [x] `UpDownClient::reqFileAICHHash()` / `isSupportingAICH()` / `isAICHReqPending()` getters

---

### Module 12: Download Queue & Transfer Engine

- [x] Port `DownloadQueue.cpp/h` — file management, init from temp dirs, source management, priority sorting
- [x] Port `UploadQueue.cpp/h` — slot allocation, score-based selection, data rate tracking
- [x] Port `UploadBandwidthThrottler.cpp/h` — `QThread` with `std::condition_variable` sync
- [x] Port `UploadDiskIOThread.cpp/h` — `QThread` queue-based disk reads, zlib compression
- [x] Port `Scheduler.cpp/h` — time-based speed/connection scheduling with QSettings persistence
- [x] Port `Import_Parts.cpp/h` — minimal stub (MFC source doesn't exist in srchybrid/)

---

### Module 13: Search Engine

- [x] Port `SearchList.cpp/h` (search result management)
- [x] Port `SearchFile.cpp/h`
- [x] Port `SearchParams.cpp/h`
- [x] Port `SearchExpr.cpp/h` (boolean expression builder)
- [x] Port `Parser.cpp/h` + `Scanner.l` → `SearchExprParser.h/.cpp` (recursive descent parser for user search queries)

---

### Module 14: IP Filter & Security

- [x] Port `IPFilter.cpp/h` (IP range filtering) — `ipfilter/IPFilter.h/.cpp`, QObject with signals, value-semantic `IPFilterEntry`, binary-search lookup, FilterDat/PeerGuardian/PeerGuardian2 parsers, sort & merge
- [ ] Port `IPFilterDlg.cpp/h` → Qt dialog

---

### Module 15: Statistics & Logging

- [x] Port `Statistics.cpp/h` — QObject-based Statistics class with getters, signals, overhead tracking, rate averaging
- [x] Port `StatisticFile.cpp/h` — moved to Module 11 Phase 1

---

### Module 16: Preferences & Configuration

- [x] Port `Preferences.cpp/h` — Phase 1: ~50 essential settings with YAML persistence (yaml-cpp), thread-safe `Preferences` class with factory methods for `ObfuscationConfig` / `ProxySettings`
- [ ] Ensure all preference keys are preserved for migration from Windows eMule

---

### Module 17: Chat & IRC Subsystem

- [x] Port `IrcMain.cpp/h` (IRC protocol logic) — `chat/IrcMessage.h` (RFC 2812 parser), `chat/IrcClient.h/.cpp` (QObject signal-based IRC client with auto PING/PONG, CTCP, login sequence, numeric dispatch)
- [x] Port `IrcSocket.cpp/h` — replaced by `QTcpSocket` inside `IrcClient`, UTF-8/Latin1 encoding, buffered line-based read
- [x] Port `ChatWnd.cpp/h` → `QWidget` — `gui/panels/MessagesPanel.h/.cpp` (friend-based messaging panel)
- [x] Port `ChatSelector.cpp/h` → `QTabWidget` — integrated into MessagesPanel

---

### Module 18: Friend System

- [x] Port `Friend.cpp/h` — `friends/Friend.h/.cpp` (data class with user hash, Kad ID, IP/port, name, timestamps, friend slot; binary serialization via SafeFile + Tag matching emfriends.met format)
- [x] Port `FriendList.cpp/h` — `friends/FriendList.h/.cpp` (QObject collection manager with load/save emfriends.met, add/remove/search, duplicate detection, signals for GUI)

---

### Module 19: Web Server (Built-in HTTP Server)

- [x] Port `WebServer.cpp/h` (172KB — very large) — replaced with JSON REST API using `QHttpServer`, plus classic template engine
- [x] Port `WebSocket.cpp/h` — subsumed by REST API (React frontend uses fetch, not WebSocket)
- [x] Consider using `QHttpServer` (Qt 6.4+) as replacement — implemented with `QHttpServer`
- [x] `WebTemplateEngine` — parses eMule .tmpl files with `<--TMPL_NAME-->` sections, `[Key]` substitution
- [x] `WebSessionManager` — SHA-256 password login, session timeout, admin/guest roles
- [x] `JsonSerializers.h` — JSON serializers for REST API responses

---

### Module 20: Media & Preview

- [x] Port `Preview.cpp/h` — `PreviewApps` (config parser) + `PreviewThread` (background copy + launch)
- [x] Port `MediaInfo.cpp/h`
- [x] Port `FrameGrabThread.cpp/h` — `QThread` + `QMediaPlayer`/`QVideoSink`
- [ ] Port `3DPreviewControl.cpp/h` → `QOpenGLWidget` or remove (deferred to GUI module — needs Qt6::Widgets)
- [ ] Port `TextToSpeech.cpp/h` — excluded per user request

---

### Module 21: GUI — Main Application Shell

Replace the MFC application framework with Qt Widgets. The GUI connects to the `emulecored` daemon via IPC (see Module 31) and auto-launches it if needed.

- [x] Port `Emule.cpp/h` (`CWinApp` → `QApplication`) — `gui/app/main.cpp` (loads prefs, creates MainWindow, manages IpcClient lifecycle, auto-launches daemon)
- [x] Port `EmuleDlg.cpp/h` (`CDialog` → `QMainWindow`) — `gui/app/MainWindow.h/.cpp` (toolbar with 32×32 icons, stacked tab pages, status bar with up/down speed + user count + eD2k/Kad status)
- [x] Port `ToolbarWnd.cpp/h` / `MuleToolBarCtrl.cpp/h` → `QToolBar` — integrated into `MainWindow` (tab buttons: Kad, Servers, Transfers, Search, SharedFiles, Messages, IRC, Statistics + Connect/Disconnect)
- [x] Port `MuleStatusBarCtrl.cpp/h` → `QStatusBar` — integrated into `MainWindow` (status message, users, up/down speeds, eD2k, Kad labels)
- [x] IPC client for daemon communication — `gui/app/IpcClient.h/.cpp` (connects to daemon, request/callback pattern with seqId, push event dispatch, auto-subscribe)
- [x] Port `SplashScreen.cpp/h` → `QSplashScreen` — implemented in `gui/app/main.cpp` (Logo.jpg splash on startup)
- [x] Port `Wizard.cpp/h` → `QWizard` — `gui/dialogs/FirstStartWizard.h/.cpp`
- [x] Port `MiniMule.cpp/h` → `QWidget` (floating info window) — `gui/app/MiniMuleWidget.h/.cpp`
- [x] Port `MuleSystrayDlg.cpp/h` → `QSystemTrayIcon` — integrated into MainWindow (tray icon with connection status icons, double-click restore, single-click MiniMule)
- [x] Port `DialogMinTrayBtn.cpp/h` → not needed; MainWindow closeEvent + QSystemTrayIcon handles minimize-to-tray
- [x] Port `TrayDialog.cpp/h` → not needed; MainWindow directly manages QSystemTrayIcon
- [x] Port `TaskbarNotifier.cpp/h` → `QSystemTrayIcon::showMessage()` — implemented in MainWindow
- [x] Port `ExitBox.cpp/h` → `QMessageBox` (used in 10 files for confirmations/warnings)
- [x] Port `TrayMenuBtn.cpp/h` → `QMenu` with `QWidgetAction` speed controls (tray context menu in MainWindow)
- [x] Port `resource.h` → Qt resource system (`.qrc`) — `resources/emuleqt.qrc` with 200+ icons
- [x] Migrate icons/bitmaps from `res/` to Qt resource file — all icons in `resources/icons/` and `resources/smileys/`
- [x] `Ed2kSchemeHandler` — `gui/app/Ed2kSchemeHandler.h/.cpp` (ed2k:// URL scheme handler)
- [x] `AutoStart` — `gui/app/AutoStart.h/.cpp` (platform-specific autostart registration)
- [x] `PowerManager` — `gui/app/PowerManager.h/.cpp` (prevents sleep during transfers)
- [x] `VersionChecker` — `gui/app/VersionChecker.h/.cpp` (checks for updates)
- [x] `UiState` — `gui/app/UiState.h/.cpp` (UI state persistence)
- [x] `CommandLineExec` (GUI) — `gui/app/CommandLineExec.h/.cpp` (CLI argument handling for GUI)
- [x] `CoreConnectDialog` — `gui/dialogs/CoreConnectDialog.h/.cpp` (daemon connection dialog)

---

### Module 22: GUI — Tab/Page Panels

- [x] Port `TransferDlg.cpp/h` / `TransferWnd.cpp/h` → `QWidget` (Transfers tab) — `gui/panels/TransferPanel.h/.cpp`
- [x] Port `SearchDlg.cpp/h` / `SearchResultsWnd.cpp/h` / `SearchParamsWnd.cpp/h` → `QWidget` (Search tab) — `gui/panels/SearchPanel.h/.cpp`
- [x] Port `SharedFilesWnd.cpp/h` → `QWidget` (Shared Files tab) — `gui/panels/SharedFilesPanel.h/.cpp`
- [x] Port `ServerWnd.cpp/h` → `QWidget` (Servers tab) — `gui/panels/ServerPanel.h/.cpp` (server list view, connect/disconnect, add server form, update server.met, My Info, LogWidget with Server Info/Log/Verbose tabs; uses both direct core access and IPC)
- [x] Port `KademliaWnd.cpp/h` → `QWidget` (Kademlia tab) — `gui/panels/KadPanel.h/.cpp` (contacts tree, bootstrap controls, firewall check, ContactsGraph histogram + network graph, searches table; IPC-driven with 2s refresh timer)
- [x] Port `StatisticsDlg.cpp/h` → `QWidget` (Statistics tab) — `gui/panels/StatisticsPanel.h/.cpp`
- [x] Port `IrcWnd.cpp/h` → `QWidget` (IRC tab) — `gui/panels/IrcPanel.h/.cpp`
- [x] Port `ChatWnd.cpp/h` → `QWidget` (Messages tab) — `gui/panels/MessagesPanel.h/.cpp`

---

### Module 23: GUI — Custom Controls

Replace MFC custom controls with Qt equivalents.

- [x] Port `MuleListCtrl.cpp/h` → not needed; each list uses its own `QTreeView` + `QAbstractItemModel` directly
- [x] Port `ListCtrlX.cpp/h` → not needed; Qt model/view handles this natively
- [x] Port `ListCtrlEditable.cpp/h` → not needed; `QStyledItemDelegate` used where editing is required
- [x] Port `ListCtrlItemWalk.cpp/h` → not needed; model-based navigation handled by Qt selection model
- [x] Port `DownloadListCtrl.cpp/h` → `QTreeView` + custom model — `gui/controls/DownloadListModel.h/.cpp`
- [x] Port `UploadListCtrl.cpp/h` → `QTreeView` + `ClientListModel(Uploading)` — reuses `ClientListModel` in upload mode (TransferPanel)
- [x] Port `SharedFilesCtrl.cpp/h` → `QTreeView` + custom model — `gui/controls/SharedFilesModel.h/.cpp`
- [x] Port `ServerListCtrl.cpp/h` → `QTreeView` + custom model — `gui/controls/ServerListModel.h/.cpp` (columns: Name, IP:Port, Description, Ping, Users, MaxUsers, Preference, Failed, Static, SoftFiles, LowID, Obfuscation; refreshFromServerList + context menu support)
- [x] Port `ClientListCtrl.cpp/h` → `QTreeView` + custom model — `gui/controls/ClientListModel.h/.cpp`
- [x] Port `SearchListCtrl.cpp/h` → `QTreeView` + custom model — `gui/controls/SearchResultsModel.h/.cpp`
- [x] Port `KadSearchListCtrl.cpp/h` → `QTreeView` + custom model — `gui/controls/KadSearchesModel.h/.cpp` (columns: Number, Key, Type, Name, Status, Load, PacketsSent, Responses)
- [x] Port `KadContactListCtrl.cpp/h` → `QTreeView` + custom model — `gui/controls/KadContactsModel.h/.cpp` (columns: Status icon, ClientId hex, Distance binary; 5 colored circle icons indicating contact quality; Courier New 8pt font)
- [x] Port `KadContactHistogramCtrl.cpp/h` → `QWidget` with custom paint — `gui/controls/ContactsGraph.h/.cpp` (deque-based bar chart, 120 samples, auto-scaled Y-axis) + `gui/controls/KadContactHistogram.h/.cpp`
- [x] Port `KadLookupGraph.cpp/h` → `QWidget` with custom paint — `gui/controls/KadLookupGraph.h/.cpp`
- [ ] Port `DownloadClientsCtrl.cpp/h` → `QTreeView` + custom model (sources per file)
- [x] Port `FriendListCtrl.cpp/h` → `QTreeView` + custom model — `gui/controls/FriendListModel.h/.cpp`
- [ ] Port `CollectionListCtrl.cpp/h` → `QTreeView` + custom model
- [ ] Port `CommentListCtrl.cpp/h` → `QTreeView` + custom model
- [x] Port `IrcChannelListCtrl.cpp/h` → `QTreeWidget` in IrcPanel (`m_channelListWidget`)
- [x] Port `IrcNickListCtrl.cpp/h` → `QListView` + `QStringListModel` in IrcPanel (`m_nickListView`)
- [x] Port `SharedDirsTreeCtrl.cpp/h` → not needed; SharedFilesPanel uses flat list approach
- [x] Port `DirectoryTreeCtrl.cpp/h` → `CheckableFileSystemModel` (QFileSystemModel subclass) in OptionsDialog
- [x] Port `TabCtrl.cpp/h` / `ClosableTabCtrl.cpp/h` / `ButtonsTabCtrl.cpp/h` → `QTabWidget` (used in KadPanel, SharedFilesPanel, IrcPanel, FileDetailDialog)
- [x] Port `IrcChannelTabCtrl.cpp/h` → `QTabWidget` in IrcPanel
- [x] Port `OScopeCtrl.cpp/h` → `QWidget` with custom paint — `gui/controls/StatsGraph.h/.cpp`
- [x] Port `StatisticsTree.cpp/h` → `QTreeWidget` in StatisticsPanel (`m_tree`)
- [x] Port `BarShader.cpp/h` → custom `QWidget::paintEvent()` with `QPainter` — `gui/controls/DownloadProgressDelegate.h/.cpp` (progress bar delegate for download list) + `gui/controls/SharedPartsDelegate.h/.cpp` (parts visualization for shared files)
- [x] Port `ProgressCtrlX.cpp/h` → `QProgressBar` used in SharedFilesPanel, FirstStartWizard, ImportDownloadsDialog, ArchivePreviewPanel
- [x] Port `SplitterControl.cpp/h` → `QSplitter` (used in 18 files across all panels)
- [x] Port `ToolTipCtrlX.cpp/h` → not needed; Qt native `setToolTip()` used throughout (19 uses in 7 files)
- [x] Port `HTRichEditCtrl.cpp/h` / `RichEditCtrlX.cpp/h` → `QTextBrowser` — `gui/controls/LogWidget.h/.cpp` (tabbed log display: Server Info, Log, Verbose, Kad; Qt message handler integration; append methods per category)
- [x] Port `TreeOptionsCtrl.cpp/h` / `TreeOptionsCtrlEx.cpp/h` → not needed; OptionsDialog uses `QListWidget` sidebar + `QStackedWidget`
- [ ] Port `DropTarget.cpp/h` → Qt drag-and-drop (`QMimeData`, `dragEnterEvent`)
- [x] Port `DropDownButton.cpp/h` → `QToolButton` with `setMenu()`/`setPopupMode()` (used in MainWindow)
- [x] Port `ColorButton.cpp/h` → `QPushButton` + `QColorDialog` (used in OptionsDialog)
- [ ] Port `BuddyButton.cpp/h` → `QPushButton` (Kad buddy indicator in prefs pages)
- [x] Port `GradientStatic.cpp/h` → not needed; was only used in MuleSystrayDlg (now replaced by QSystemTrayIcon)
- [x] Port `IconStatic.cpp/h` → not needed; Qt panels use `QLabel` with icons / toolbar icons directly
- [x] Port `ComboBoxEx2.cpp/h` → not needed; `QComboBox` used directly throughout
- [ ] Port `EditX.cpp/h` / `EditDelayed.cpp/h` → `QLineEdit` with delayed signal
- [x] Port `InputBox.cpp/h` → `QInputDialog` (used in OptionsDialog, SharedFilesPanel, IrcPanel, TransferPanel, MessagesPanel)
- [x] Port `ListBoxST.cpp/h` → not needed; no usages beyond itself in original code
- [x] Port `ListViewSearchDlg.cpp/h` → not needed; Qt model/view has built-in find; was only used in MuleListCtrl base class
- [x] Port `SmileySelector.cpp/h` → smiley popup in IrcPanel and MessagesPanel
- [x] Port `CustomAutoComplete.cpp/h` → `QCompleter` (used in SearchPanel)
- [x] Port `ColourPopup.cpp/h` → `QColorDialog` (used in OptionsDialog)
- [x] Port `TransferToolbar` — `gui/controls/TransferToolbar.h/.cpp` (transfer panel-specific toolbar)

---

### Module 24: GUI — Dialogs

- [x] Port `PreferencesDlg.cpp/h` → `QDialog` with `QStackedWidget` — `gui/dialogs/OptionsDialog.h/.cpp` (left sidebar with category icons, right stacked pages)
- [x] Port all `PPg*.cpp/h` (17 preference pages) → `QWidget` pages — all integrated into OptionsDialog: General, Display, Connection, Proxy, Server, Directories, Files, Notifications, IRC, Messages/Comments, Security, Scheduler, Statistics, Extended, Web Interface
- [x] Port `FileDetailDialog.cpp/h` + info/name/statistics variants → `QDialog` — `gui/dialogs/FileDetailDialog.h/.cpp` (7 tabs: General, File Names, Comments, Media Info, Metadata, ED2K Link, Archive Preview)
- [x] Port `FileInfoDialog.cpp/h` → integrated as Media Info tab in FileDetailDialog
- [x] Port `MetaDataDlg.cpp/h` → integrated as Metadata tab in FileDetailDialog (ED2K tag table)
- [x] Port `CommentDialog.cpp/h` / `CommentDialogLst.cpp/h` → integrated as Comments tab in FileDetailDialog
- [x] Port `ClientDetailDialog.cpp/h` → `QDialog` — `gui/dialogs/ClientDetailDialog.h/.cpp`
- [ ] Port `CatDialog.cpp/h` → `QDialog` (download categories)
- [x] Port `AddFriend.cpp/h` → `QDialog` — `gui/dialogs/AddFriendDialog.h/.cpp`
- [ ] Port `AddSourceDlg.cpp/h` → `QDialog`
- [x] Port `DirectDownloadDlg.cpp/h` → `QDialog` — `gui/dialogs/PasteLinksDialog.h/.cpp` (paste ed2k links dialog)
- [x] Port `NetworkInfoDlg.cpp/h` → `QDialog` — `gui/dialogs/NetworkInfoDialog.h/.cpp`
- [x] Port `ArchivePreviewDlg.cpp/h` → integrated as Archive Preview tab in FileDetailDialog + standalone `gui/dialogs/ArchivePreviewPanel.h/.cpp`
- [ ] Port `PreviewDlg.cpp/h` → `QDialog`
- [ ] Port `CollectionCreateDialog.cpp/h` / `CollectionViewDialog.cpp/h` → `QDialog`
- [ ] Port `CreditsDlg.cpp/h` / `CreditsThread.cpp/h` → `QDialog`
- [x] Port `TreePropSheet.cpp/h` / `TreePropSheetPgFrame.cpp/h` → replaced by OptionsDialog's QListWidget sidebar + QStackedWidget pattern
- [ ] Port `ListViewWalkerPropertySheet.cpp/h` → `QDialog` with navigation
- [ ] Port `SMTPdialog.cpp/h` / `SendMail.cpp/h` → `QDialog`
- [ ] Port `IPFilterDlg.cpp/h` → `QDialog`
- [x] `ImportDownloadsDialog` — `gui/dialogs/ImportDownloadsDialog.h/.cpp` (import downloads from other clients)
- [x] `CoreConnectDialog` — `gui/dialogs/CoreConnectDialog.h/.cpp` (daemon connection dialog)
- [x] `FirstStartWizard` — `gui/dialogs/FirstStartWizard.h/.cpp` (first-run setup wizard)

---

### Module 25: GUI — Graphics Utilities

- [x] Port `MemDC.cpp/h` → not needed (Qt double-buffers by default)
- [x] Port `Drawgdix.cpp/h` → `QPainter` helpers (QPainter used in 18+ files)
- [x] Port `EnBitmap.cpp/h` → `QPixmap` / `QImage` (Qt native image handling)
- [x] Port `Quantize.cpp/h` → `QImage` color manipulation (not needed)
- [x] Port `MeterIcon.cpp/h` → `QIcon` with dynamic painting (not needed)
- [x] Port `GDIThread.cpp/h` → not needed with Qt
- [x] Port `LayeredWindowHelperST.cpp/h` → `QWidget::setWindowOpacity()` (used in MiniMuleWidget)
- [x] Remove `VisualStylesXP.cpp/h` (not needed with Qt)
- [x] Remove `dxtrans.cpp/h` (DirectX transitions — not applicable)

---

### Module 26: Localization

- [x] Convert `.rc` string tables to Qt `.ts` translation files — `lang/` directory with 9 languages (en, de_DE, es_ES, fr_FR, it_IT, ja_JP, ko_KR, pt_BR, zh_CN), both `.ts` and compiled `.qm` files
- [x] Set up `lupdate` / `lrelease` workflow in CMake — `src/gui/CMakeLists.txt` uses `qt_add_translations()` with `QM_FILES_OUTPUT_VARIABLE`, requires `Qt6::LinguistTools`
- [x] Wrap all user-visible strings in `tr()` or `QCoreApplication::translate()` (1055+ uses across 27+ GUI files)
- [ ] Migrate remaining language files from `.rc` format to `.ts` format (9 of 131 done)
- [ ] Port `langids.cpp/h`

---

### Module 27: Resource Migration

- [x] Convert icons from `res/` to Qt resource system (`.qrc`) — 200+ icons in `resources/emuleqt.qrc`
- [x] Convert toolbar bitmaps to individual icons — individual `.ico` files in resource system
- [x] Replace `.rc` dialog templates with Qt `.ui` files or C++ widget code — all dialogs built in C++ code
- [x] Migrate menu definitions from `.rc` to `QMenuBar` / `QMenu` in code — QMenu used in 17+ files
- [ ] Migrate accelerator tables to `QShortcut` / `QAction::setShortcut()` — no keyboard shortcuts implemented yet
- [ ] Port `TitleMenu.cpp/h` → `QMenu` with custom title painting — context menus exist (13 panels) but lack title headers

---

### Module 28: Third-Party Dependencies

- [x] Integrate **zlib** via CMake `find_package(ZLIB)` — used in Packet.cpp, DownloadClient.cpp, ArchiveRecovery.cpp, UploadDiskIOThread.cpp
- [x] Replace **CxImage** with `QImage` / `QPixmap` — QImage used in FrameGrabThread, captcha generation, preview answers; no CxImage in src/
- [x] Integrate **miniupnpc** via CMake — FetchContent miniupnpc 2.3.0, fully implemented in UPnPManager.h/cpp
- [x] Replace custom MD4/SHA with `QCryptographicHash` — MD4Hasher, MD5Hasher, ShaHasher all wrap QCryptographicHash; CryptoPP fully removed
- [x] Remove Windows-only dependencies — no afxwin.h/atlcoll.h; winsock2.h properly `#ifdef Q_OS_WIN` guarded with arpa/inet.h fallback
- [x] Integrate **OpenSSL** (optional) — `find_package(OpenSSL)`, used in EncryptedStreamSocket for RC4 obfuscation
- [x] Integrate **yaml-cpp** 0.8.0 via FetchContent — used for Preferences YAML persistence
- [x] Integrate **libarchive** 3.7.7 via FetchContent — ArchiveReader supports 30+ formats (ZIP, RAR, 7z, etc.)

---

### Module 29: Unit Tests — Test Infrastructure

Set up the Qt Test framework infrastructure. Each test is a standalone executable using
`QTest` (see [Qt Test Overview](https://doc.qt.io/qt-6/qtest-overview.html)). Tests use
`QVERIFY`, `QCOMPARE`, `QFETCH` (data-driven), `QBENCHMARK`, and `QSignalSpy` for
signal verification. All test classes inherit `QObject` and use `Q_OBJECT`.

- [x] Create `tests/` directory with its own `CMakeLists.txt` — auto-discovers `tst_*.cpp` files
- [x] Add `enable_testing()` and `find_package(Qt6 REQUIRED COMPONENTS Test)` to root CMake
- [x] Set up CTest integration so `ctest` runs all test suites
- [x] Create `tests/TestHelpers.h` — shared utilities (temp dirs, mock data factories, fixture base class)
- [x] Create `tests/data/` directory for test fixtures — `tests/data/` with `ipfilter_sample.dat`, `ipfilter_peerguardian.txt`
- [ ] Configure CI pipeline to run tests on Linux, macOS, Windows
- [ ] Port `SelfTest.cpp/h` → Qt Test as `tst_SelfTest.cpp`

---

### Module 29a: Unit Tests — Platform Abstraction (Module 2)

- [x] `tst_TypeDefs.cpp` — sizeof/signedness for all Types.h + WinCompat.h aliases, EMFileSize==uint64, pointer-sized types
- [x] `tst_StringConversion.cpp` — `QString` ↔ UTF-8 round-trip, hex conversion, `formatByteSize`, `formatDuration`, `EMUSTR` macro
- [x] `tst_PathUtils.cpp` — `executablePath`, separator handling, `canonicalPath`, `pathsEqual`, `sanitizeFilename`, `appDirectory`, `freeDiskSpace`
- [x] `tst_TimeUtils.cpp` — `getTickCount` monotonicity, `elapsedMs` accuracy, `sleepMs`, `HighResTimer`, `fromTimeT`/`toTimeT` roundtrip
- [x] `tst_AtomicOps.cpp` — multi-thread `Atomic` stress test, `ManualResetEvent` set/wait/reset, `AutoResetEvent` single-wake, `Mutex` RAII + contention, `waitFor` timeout

---

### Module 29b: Unit Tests — Core Utilities (Module 3)

- [x] `tst_OtherFunctions.cpp` — MD4 helpers, base16/32 encode/decode, URL encode/decode, IP helpers, RC4 encrypt/decrypt, file type detection, peek/poke, string helpers, Levenshtein distance
- [x] `tst_SafeFile.cpp` — SafeMemFile typed I/O (uint8/16/32/64, hash16, string encodings), SafeFile write/read with seek, readPastEnd exception, takeBuffer
- [x] `tst_Ini2.cpp` — covered by Module 29a's `tst_StringConversion.cpp` (QSettings tested via SettingsUtils)
- [x] `tst_SearchExprParser.cpp` — keywords, implicit/explicit AND/OR/NOT, dash-NOT, parentheses, quoted strings, all attribute filters (@size/@type/@ext/@sources/@complete/@bitrate/@length/@codec/@rating/@title/@album/@artist), comparison operators, error cases
- [x] `tst_Log.cpp` — LogFile create/write/rotation/reopen, Opcodes compile-time checks (PARTSIZE, time macros, protocol headers), MapKey equality, Exceptions hierarchy, PerfLog no-op
- [x] `tst_TimeTick.cpp` — covered by Module 29a's `tst_TimeUtils.cpp` (HighResTimer tested there)

---

### Module 29c: Unit Tests — Cryptography & Hashing (Module 4)

Verify hash output against known test vectors (RFC 1320 for MD4, RFC 1321 for MD5, FIPS 180-4 for SHA).

- [x] `tst_MD4.cpp` — RFC 1320 test vectors, empty input, reset+reuse, chunked add
- [x] `tst_MD5.cpp` — RFC 1321 test vectors, construct from string/data, hash string format
- [x] `tst_SHA.cpp` — SHA-1 known vectors, AICHHashAlgo interface, Base32 roundtrip, hashFromString/URN, isNull
- [x] `tst_AICHHashTree.cpp` — construction, findHash, setBlockHash, reCalculateHash, verifyHashTree valid/corrupt, roundtrip
- [x] `tst_AICHHashSet.cpp` — AICH hashset load/save, verification
- [x] `tst_FileIdentifier.cpp` — MD4/AICH get/set, compareRelaxed/Strict, writeIdentifier roundtrip, hashset load/write, calculateMD4ByHashSet, AICH verify, writeHashSetsToPacket roundtrip, FileIdentifierSA readIdentifier, theoretical counts
- [ ] `tst_CaptchaGenerator.cpp` — deferred (needs Qt6::Gui)
- [ ] `tst_HashPerformance.cpp` — `QBENCHMARK` for MD4/SHA-256 throughput on 1MB, 10MB, 100MB data
- [ ] `tst_QCryptographicHashCompat.cpp` — verify custom implementations match `QCryptographicHash` output

---

### Module 29d: Unit Tests — Networking & Sockets (Module 5)

Use `QTcpServer` / `QTcpSocket` loopback and `QSignalSpy` for async signal testing.

- [x] `tst_Packet.cpp` — construction, header serialization, pack/unpack roundtrip, RawPacket, detachPacket
- [x] `tst_EMSocket.cpp` — packet framing, partial reassembly, multiple packets, wrong header/oversized rejection, rate limiting
- [ ] `tst_ThrottledSocket.cpp` — bandwidth limiter accuracy (measure bytes/sec over time window)
- [ ] `tst_EncryptedStreamSocket.cpp` — RC4 handshake over loopback, obfuscation roundtrip
- [x] `tst_EncryptedDatagram.cpp` — ED2K/Kad/Server encrypt-decrypt roundtrips, overhead size, passthrough
- [x] `tst_ListenSocket.cpp` — accept incoming connections, port binding
- [ ] `tst_ProxySocket.cpp` — SOCKS4/SOCKS5/HTTP proxy negotiation (mock proxy server)
- [ ] `tst_HttpClient.cpp` — `QNetworkAccessManager` GET/POST, redirect following, timeout handling
- [x] `tst_Pinger.cpp` — ICMP echo to localhost, invalid address handling, sequential pings, PingStatus defaults (uses QSKIP when ICMP socket unavailable)
- [x] `tst_TcpConnect.cpp` — TCP localhost connectivity test, connection establishment verification
- [x] `tst_ClientUDPSocket.cpp` — client UDP socket tests
- [x] `tst_UDPSocket.cpp` — server UDP socket tests
- [x] `tst_ServerSocket.cpp` — server socket connection tests
- [x] `tst_HttpClientReqSocket.cpp` — HTTP client request socket tests
- [x] `tst_LastCommonRouteFinder.cpp` — last common route finder tests
- [ ] `tst_SocketStress.cpp` — open/close 100 connections rapidly, verify no resource leaks

---

### Module 29e: Unit Tests — Firewall & UPnP (Module 6)

- [x] `tst_UPnPManager.cpp` — construction, default state, safe no-ops, signal emission
- [x] ~~`tst_UPnPDiscovery.cpp`~~ — consolidated into `tst_UPnPManager.cpp`
- [x] ~~`tst_UPnPPortMapping.cpp`~~ — consolidated into `tst_UPnPManager.cpp`
- [x] ~~`tst_FirewallOpener.cpp`~~ — removed (FirewallOpener removed)

---

### Module 29f: Unit Tests — ED2K Protocol & Packets (Module 7)

- [ ] `tst_Opcodes.cpp` — verify opcode constants match ED2K protocol specification
- [ ] `tst_PacketSerialization.cpp` — serialize/deserialize every packet type, verify binary layout
- [ ] `tst_PacketRoundTrip.cpp` — construct packet → serialize → deserialize → compare (data-driven with `QFETCH`)
- [ ] `tst_PacketMalformed.cpp` — truncated packets, oversized packets, invalid opcodes → graceful error
- [x] `tst_ED2KLink.cpp` — parse valid ed2k:// file/server/serverlist/node/search links, magnet links, invalid links, URL encoding (27 tests)
- [x] `tst_Tags.cpp` — tag construction (uint32/64, string, float, hash, blob), old/new format round-trips, size optimization, string/ID name serialization, edge cases, mutators (31 tests)
- [ ] `tst_Endianness.cpp` — verify little-endian wire format on big-endian and little-endian hosts

---

### Module 29g: Unit Tests — Server Management (Module 8)

- [x] `tst_Server.cpp` — construct server, get/set properties, serialization to/from `server.met` format
- [x] `tst_ServerList.cpp` — load/save `server.met`, add/remove servers, duplicate detection, merge lists
- [x] `tst_ServerConnect.cpp` — connection state machine transitions, timeout, retry logic
- [x] `tst_ServerMetData.cpp` — server.met binary data parsing tests
- [ ] `tst_ServerListURL.cpp` — download server list from HTTP (mock `QNetworkAccessManager`), parse response

---

### Module 29h: Unit Tests — Client Management & Credits (Module 9)

- [x] `tst_ClientStateDefs.cpp` — enum types, values, protocol constants
- [x] `tst_ClientCredits.cpp` — credit calculation, score computation, serialization to `clients.met`, identity state machine
- [x] `tst_ClientList.cpp` — add/remove/find clients by hash/IP/port, duplicate handling
- [x] `tst_DeadSourceList.cpp` — add dead source, expiry timeout, re-addition after timeout
- [x] `tst_CorruptionBlackBox.cpp` — record corrupted block, identify responsible client, evaluation
- [x] `tst_UpDownClient.cpp` — client state transitions (connecting, handshake, requesting, transferring)
- [x] `tst_URLClient.cpp` — URL client (HTTP download) tests

---

### Module 29i: Unit Tests — Kademlia / DHT (Module 10)

- [x] `tst_KadUInt128.cpp` — arithmetic (add, subtract, XOR), comparison, bit operations, hex/binary strings, byte-array round-trip (Phase 1)
- [x] `tst_KadContact.cpp` — construct contact, distance calculation (XOR metric), type progression, IP change, copy (Phase 1)
- [x] `tst_KadRoutingBin.cpp` — add/remove contacts, K limit, global IP/subnet limits, closest-to ordering, LAN detection (Phase 1)
- [x] `tst_KadRoutingZone.cpp` — zone splitting, contact lookup, closest-nodes query, consolidation, write/read round-trip (Phase 2)
- [x] `tst_KadSearch.cpp` — search initiation, response processing, result aggregation, timeout
- [x] `tst_KadSearchManager.cpp` — concurrent searches, search deduplication, result callbacks
- [x] `tst_KadIndexed.cpp` — store/retrieve keyword→file entries, expiration, storage limits
- [x] `tst_KadEntry.cpp` — entry creation, tag parsing, lifetime management
- [x] `tst_KadPrefs.cpp` — KadID generation, IP two-step verification, firewall counters, UDP verify key, external port consensus (Phase 2)
- [x] `tst_KadIO.cpp` — UInt128 round-trip, Kad tag read/write, tag list serialization
- [x] `tst_KadUDPListener.cpp` — packet dispatch, receive handler invocation, protocol message processing
- [x] `tst_KadPacketTracking.cpp` — track sent packets, detect timeouts, remove acknowledged
- [x] `tst_KadFirewallTester.cpp` — firewall test state machine, result callback
- [x] `tst_KadUDPKey.cpp` — key generation, verification, expiry (consolidated into `tst_KadPrefs.cpp`)
- [x] `tst_KadLookupHistory.cpp` — record lookup steps, retrieve history, max history size
- [x] `tst_Kademlia.cpp` — engine start/stop, static instance, bootstrap, process timer
- [x] `tst_KadMiscUtils.cpp` — keyword hashing, word splitting, IP validation
- [x] `tst_KadNodesData.cpp` — nodes.dat binary data parsing tests
- [ ] `tst_KadIntegration.cpp` — bootstrap with mock peers, publish keyword, search keyword, find node (integration test with multiple Kademlia instances in-process)

---

### Module 29j: Unit Tests — File Management (Module 11)

- [x] `tst_AbstractFile.cpp` — base file properties (name, size, hash), tags, ED2K link
- [x] `tst_ShareableFile.cpp` — path storage, shared directory logic, file type round-trip, info summary
- [x] `tst_StatisticFile.cpp` — counter arithmetic, all-time setters, merge correctness
- [x] `tst_CollectionFile.cpp` — construct from AbstractFile, write/read round-trip, ed2k link init
- [x] `tst_KnownFile.cpp` — part count edge cases, priority validation, load/write round-trip, purge check
- [x] `tst_KnownFileList.cpp` — load/save `known.met`, lookup by hash, duplicates, cancelled.met
- [x] `tst_SharedFileList.cpp` — scan directory, add/remove shared files, server publish no-ops, Kad publish
- [x] `tst_SharedFileData.cpp` — shared file data persistence tests
- [x] `tst_PartFile.cpp` — create new download, gap list management, flush to disk, resume from `.part`/`.part.met` (29 tests)
- [x] `tst_PartFileConvert.cpp` — format detection, job management, thread start/stop, processQueue
- [x] `tst_PartFileWriteThread.cpp` — async write correctness, write ordering, flush-on-close
- [x] `tst_PartFileData.cpp` — part file binary data tests
- [ ] `tst_Collection.cpp` — create collection, add files, serialize/deserialize `.emulecollection`
- [x] `tst_ArchiveRecovery.cpp` — ZIP/RAR recovery, async null guard, ISO/ACE detection stubs
- [x] `tst_ArchiveReader.cpp` — libarchive-based reader, format detection, entry listing
- [ ] `tst_ZIPFile.cpp` — list entries, extract files, handle corrupted ZIP
- [ ] `tst_GZipFile.cpp` — decompress gzip stream, handle truncated input

---

### Module 29k: Unit Tests — Download Queue & Transfer Engine (Module 12)

- [x] `tst_DownloadQueue.cpp` — add/remove downloads, priority ordering, init scan, source management (13 tests)
- [x] `tst_UploadQueue.cpp` — construction, add/remove clients, IP limits, data rates, process (15 tests)
- [x] `tst_UploadBandwidthThrottler.cpp` — construction, start/stop, byte accounting, slot limits, pause/resume (5 tests)
- [x] `tst_UploadDiskIOThread.cpp` — construction, start/stop, compression decision, packet creation, block read (6 tests)
- [x] `tst_Scheduler.cpp` — add/remove/update schedules, save/load roundtrip, activate prefs, save/restore originals (11 tests)
- [x] `tst_ImportParts.cpp` — null/missing/empty/size-mismatch inputs, stub behavior (5 tests)

---

### Module 29l: Unit Tests — Search Engine (Module 13)

- [x] `tst_SearchExpr.cpp` — boolean expression parsing (AND, OR, NOT), nested expressions, malformed queries
- [x] `tst_SearchParams.cpp` — parameter construction, type/size/extension filters, serialization
- [x] `tst_SearchFile.cpp` — search result merging, source count aggregation, duplicate detection
- [x] `tst_SearchList.cpp` — add results, clear, filtering by type/size/availability, sorting

---

### Module 29m: Unit Tests — IP Filter & Security (Module 14)

- [x] `tst_IPFilter.cpp` — load `ipfilter.dat` (Level1 format), PeerGuardian text format, comments, sort & merge, save/load roundtrip
- [x] `tst_IPFilterMatch.cpp` — match IP against loaded ranges, boundary IPs, overlapping ranges, level checks, hit counter, signals
- [ ] `tst_IPFilterUpdate.cpp` — download filter from URL, merge with existing, reload
- [ ] `tst_IPFilterPerformance.cpp` — `QBENCHMARK` lookup time with 200k+ ranges

---

### Module 29n: Unit Tests — Statistics & Logging (Module 15)

- [x] `tst_Statistics.cpp` — session stats accumulation, transfer counters, ratio calculation, history ring buffer
- [x] `tst_StatisticFile.cpp` — per-file statistics tracking, serialization, merge with known file data
- [x] `tst_StatisticsReset.cpp` — session reset vs. cumulative stats, date-based rollover

---

### Module 29o: Unit Tests — Preferences & Configuration (Module 16)

- [x] `tst_Preferences.cpp` — Phase 1: defaults, YAML round-trip, validation clamping, factory methods (ObfuscationConfig/ProxySettings), user hash generation, random port range
- [ ] `tst_PreferencesMigration.cpp` — import preferences from legacy Windows registry / `preferences.ini` format
- [ ] `tst_PreferencesValidation.cpp` — out-of-range values clamped, invalid paths rejected, port range validation
- [ ] `tst_PreferencesCategories.cpp` — download category CRUD, default category, category-specific settings

---

### Module 29p: Unit Tests — Chat & IRC (Module 17)

- [x] `tst_IrcProtocol.cpp` — 28 tests: parse IRC messages (PRIVMSG, JOIN, PART, NICK, MODE, KICK, NOTICE, CTCP, numerics), isNumeric, numericCode
- [x] `tst_IrcClient.cpp` — 24 tests: connect/disconnect, login sequence, PING auto-response, channel/private/action messages, user events, CTCP VERSION auto-response, channel list, names, perform, send format — loopback TCP fixture
- [x] `tst_IrcLiveConnect.cpp` — live IRC server connection test
- [ ] `tst_ChatMessage.cpp` — message formatting, HTML sanitization, smileys, ed2k link detection in chat

---

### Module 29q: Unit Tests — Friend System (Module 18)

- [x] `tst_Friend.cpp` — 18 tests: construction (default, with/without hash), hash queries, accessors, friend slot, serialization roundtrip (full, name-only, kadID-only, empty)
- [x] `tst_FriendList.cpp` — 22 tests: add/remove, duplicate rejection, IP-only friends, search by hash/IP, isAlreadyFriend hex lookup, isValid, removeAllFriendSlots, save/load roundtrip, bad header, nonexistent file, signals (added/removed/loaded)

---

### Module 29r: Unit Tests — Web Server (Module 19)

- [x] `tst_WebServer.cpp` — consolidated: auth (missing/wrong/valid key), API endpoints (stats, downloads, servers, connection, friends, shared, preferences), CORS, error handling
- [x] ~~`tst_WebServerAuth.cpp`~~ — covered by `tst_WebServer.cpp` auth tests
- [x] ~~`tst_WebServerAPI.cpp`~~ — covered by `tst_WebServer.cpp` endpoint tests
- [x] ~~`tst_WebServerTemplates.cpp`~~ — N/A (JSON REST API replaces HTML templates)
- [ ] `tst_WebServerStress.cpp` — concurrent requests, connection limits, no resource leaks

---

### Module 29s: Unit Tests — Media & Preview (Module 20)

- [x] `tst_MediaInfo.cpp` — detect file type, extract metadata from test files (MP3, AVI, MKV)
- [x] `tst_PreviewApps.cpp` — config parsing, extension matching, preview-ability checks
- [x] `tst_PreviewThread.cpp` — copy + launch, error handling, temp file cleanup
- [x] `tst_FrameGrabThread.cpp` — construct/destroy, error paths, image scaling/reduction

---

### Module 29s2: Unit Tests — IPC & Daemon (Module 31)

- [x] `tst_IpcProtocol.cpp` — frame encoding (4-byte header + payload), round-trip encode/decode, edge cases (insufficient data, incomplete payload, oversized, non-array), multiple frames in buffer, message type enum constants
- [x] `tst_IpcMessage.cpp` — construction (type/seqId), CBOR array round-trip, field accessors (string/int/bool/map/array), out-of-range handling, append chaining, makeResult/makeError factories, frame round-trip
- [x] `tst_IpcConnection.cpp` — single/multiple message send/receive over TCP pair, signal emission (messageReceived, disconnected), protocol error (oversized frame), isConnected state tracking
- [x] `tst_TcpConnect.cpp` — TCP localhost connectivity test
- [ ] `tst_IpcClientHandler.cpp` — request dispatch for all message types, handshake enforcement, error responses
- [ ] `tst_IpcServer.cpp` — multi-client connections, broadcast to handshaked clients, client disconnect cleanup
- [ ] `tst_CoreNotifierBridge.cpp` — verify core signals produce correct IPC push events
- [ ] `tst_IpcClient.cpp` — handshake flow, request/callback dispatch, push event signal emission, reconnection
- [ ] `tst_DaemonApp.cpp` — start/stop lifecycle, CoreSession + IpcServer integration

---

### Module 29t: Unit Tests — GUI Widgets (Modules 21-25)

Test GUI components using `QTest::mouseClick`, `QTest::keyClick`, `QSignalSpy`, and
`QTest::qWaitForWindowExposed`. These tests verify widget behavior, not pixel-perfect rendering.

- [ ] `tst_MainWindow.cpp` — window creation, tab switching, menu actions, toolbar state
- [ ] `tst_SystemTray.cpp` — tray icon creation, context menu, show/hide on click, balloon messages
- [ ] `tst_TransferPanel.cpp` — download list population from model, context menu actions, category tabs
- [ ] `tst_SearchPanel.cpp` — search field input, results display, download from results, clear results
- [ ] `tst_ServerPanel.cpp` — server list display, connect/disconnect actions, add server dialog
- [ ] `tst_SharedFilesPanel.cpp` — shared file list, directory tree, file properties action
- [ ] `tst_StatisticsPanel.cpp` — graph rendering (non-null `QPixmap`), tree population, time range selection
- [ ] `tst_KademliaPanel.cpp` — contact list display, search initiation, graph rendering
- [ ] `tst_BarShader.cpp` — progress bar rendering with gap map, color correctness via `QImage` pixel sampling
- [ ] `tst_OScopeCtrl.cpp` — graph data update, axis scaling, history buffer
- [ ] `tst_MuleListCtrl.cpp` — sort by column, column show/hide, item selection, `QAbstractItemModel` integration
- [ ] `tst_PreferencesDialog.cpp` — open dialog, switch pages, modify setting, verify `QSettings` written on accept
- [ ] `tst_FileDetailDialog.cpp` — display file info, comment editing, name list
- [ ] `tst_WizardDialog.cpp` — page navigation (next/back/finish), validation, nick/ports set on finish
- [ ] `tst_DragDrop.cpp` — drop ed2k link onto window, drop files onto shared dirs
- [ ] `tst_Localization.cpp` — switch language at runtime, verify all visible strings translated (spot-check)

---

### Module 29u: Unit Tests — Localization (Module 26)

- [ ] `tst_TranslationLoad.cpp` — load each `.qm` file without errors, verify `QTranslator::isEmpty()` is false
- [ ] `tst_TranslationCompleteness.cpp` — compare each translation against source `.ts`, report untranslated strings
- [ ] `tst_PluralForms.cpp` — verify plural-form translations for languages with complex plural rules (Russian, Arabic, Polish)
- [ ] `tst_RTLLayout.cpp` — verify `layoutDirection()` is `RightToLeft` for Arabic/Hebrew translations

---

### Module 29v: Unit Tests — Integration & End-to-End

Larger tests that exercise multiple modules together.

- [ ] `tst_FullDownloadCycle.cpp` — create mock server + mock source client, initiate download via search, receive blocks, verify completed file hash
- [ ] `tst_FullUploadCycle.cpp` — share file, accept incoming mock client, serve requested blocks, verify credit update
- [ ] `tst_ServerReconnect.cpp` — connect to mock server, simulate disconnect, verify automatic reconnection with backoff
- [ ] `tst_KadBootstrap.cpp` — start Kademlia with known contacts, verify routing table population, perform search
- [ ] `tst_ED2KLinkHandler.cpp` — handle ed2k:// link end-to-end: parse → add to queue → resolve sources
- [ ] `tst_ConfigPersistence.cpp` — modify preferences → restart application core → verify settings persisted
- [ ] `tst_IPFilterIntegration.cpp` — load IP filter → attempt connection from filtered IP → verify rejection
- [ ] `tst_CrossPlatformFileIO.cpp` — write `.part` file on one platform format, read on another (simulate via byte-level comparison)
- [x] `tst_ServerDownloadLive.cpp` — live server connection + download test (requires network access, TCP 5662, UDP 5672)
- [x] `tst_ServerGlobalSearchLive.cpp` — live server global search test
- [x] `tst_KadLiveNetwork.cpp` — live Kademlia network connection test
- [x] `tst_FileDownloadLive.cpp` — live file download test
- [x] `tst_MockPeerDownload.cpp` — mock peer download cycle test
- [x] `tst_MockPeerUpload.cpp` — mock peer upload cycle test
- [x] `tst_SmtpSendEmail.cpp` — SMTP email notification test
- [x] `tst_Smoke.cpp` — basic smoke test for overall system health
- [ ] `tst_IpcEndToEnd.cpp` — launch daemon, connect GUI IpcClient, exercise request/response/push lifecycle end-to-end

---

### Module 30: Cleanup & Removal

- [x] Remove `Mdump.cpp/h` (Windows minidump) — not present in `src/`
- [x] Remove `VisualStylesXP.cpp/h` (Windows XP theming) — not present in `src/`
- [x] Remove `VistaDefines.h` (Windows Vista constants) — not present in `src/`
- [x] Remove `dxtrans.cpp/h` (DirectX) — not present in `src/`
- [x] Remove `qedit.h` (DirectShow) — not present in `src/`
- [x] Remove `Debug_FileSize.cpp/h` — not present in `src/`
- [x] Remove `Stdafx.h` / `Stdafx.cpp` (MFC precompiled header) — not present in `src/`
- [ ] Remove `.vcxproj`, `.sln`, `.rc` files after migration complete

---

### Module 31: IPC Layer, Daemon & GUI/Core Separation

The GUI/Core separation uses a **CBOR-over-TCP IPC protocol** rather than QVariant messages.
The architecture consists of four build targets:

- `libemulecore` — static library, headless P2P engine (Qt6::Core + Qt6::Network only)
- `libemuleipc` — static library, shared IPC protocol (Qt6::Core + Qt6::Network only)
- `emulecored` — headless daemon executable (links core + ipc)
- `emuleqt` — GUI executable (links core + ipc + Qt6::Widgets)

```
GUI Process (emuleqt)               Daemon Process (emulecored)
─────────────────────               ──────────────────────────
MainWindow                           DaemonApp
  ├─ KadPanel                         ├─ CoreSession
  ├─ ServerPanel                      │   ├─ DownloadQueue
  ├─ ...panels                        │   ├─ UploadQueue
  │                                   │   ├─ SharedFileList
  IpcClient ◄──── TCP/CBOR ────►     │   ├─ Kademlia
    (request/callback)                │   └─ ...managers
    (push event dispatch)             ├─ IpcServer
                                      │   └─ IpcClientHandler (per client)
                                      └─ CoreNotifierBridge
                                          (core signals → IPC push events)
```

#### 31a: IPC Protocol Library (`src/ipc/`)

- [x] Design length-prefixed CBOR wire protocol — `ipc/IpcProtocol.h/.cpp` (4-byte big-endian uint32 length + CBOR payload, 16 MiB max frame, protocol version "1.0")
- [x] Implement type-safe message wrapper — `ipc/IpcMessage.h/.cpp` (QCborArray-based, fluent `append()` API, `makeResult()`/`makeError()` factories, field accessors for string/int/bool/map/array)
- [x] Implement framed TCP connection — `ipc/IpcConnection.h/.cpp` (QObject wrapper over QTcpSocket, read buffer with frame assembly, signals: `messageReceived`, `disconnected`, `protocolError`)
- [x] Define all IPC message type constants — `ipc/IpcProtocol.h` (requests 100-251, responses 300-302, push events 400-520)
- [x] Implement CBOR serializers for core entities — `ipc/CborSerializers.h` (toCbor for PartFile, Server, Friend, SearchFile; status/priority string converters)
- [x] CMake target `libemuleipc` — `src/ipc/CMakeLists.txt` (links Qt6::Core + Qt6::Network only, no core dependency)

**IPC Message Types:**

| Range | Direction | Purpose |
|-------|-----------|---------|
| 100–251 | GUI → Core | Requests (Handshake, Get/Set data, actions) |
| 300–302 | Core → GUI | Responses (HandshakeOk, Result, Error) |
| 400–520 | Core → GUI | Push events (state changes, seqId=0) |

#### 31b: Daemon (`src/daemon/`)

- [x] Create `DaemonApp` orchestrator — `daemon/DaemonApp.h/.cpp` (owns CoreSession + IpcServer + CoreNotifierBridge; `start()`/`stop()` lifecycle)
- [x] Create `IpcServer` — `daemon/IpcServer.h/.cpp` (QTcpServer wrapper, manages IpcClientHandler instances, `broadcast()` to handshaked clients)
- [x] Create `IpcClientHandler` — `daemon/IpcClientHandler.h/.cpp` (per-connection request dispatcher, handshake enforcement, handle methods for all request types)
- [x] Create `CoreNotifierBridge` — `daemon/CoreNotifierBridge.h/.cpp` (connects core Qt signals to IPC push events: download add/remove, server state, stats, search results, shared files, uploads, Kad)
- [x] Create daemon entry point — `daemon/main.cpp` (QCoreApplication, loads preferences, starts DaemonApp, IPC listener)
- [x] CMake target `emulecored` — `src/daemon/CMakeLists.txt` (links eMule::Core + eMule::Ipc)
- [x] `CliIpcClient` — `daemon/CliIpcClient.h/.cpp` (CLI IPC client for daemon management)
- [x] `CommandLineExec` (daemon) — `daemon/CommandLineExec.h/.cpp` (CLI command execution for daemon)

**Request handling status:**

| Category | Status | Notes |
|----------|--------|-------|
| Downloads (get/pause/resume/cancel/stop/rename/category/details/preview) | Working | Full PartFile serialization via CborSerializers |
| Servers (list, connect, disconnect) | Working | |
| Stats | Working | Session bytes sent/received |
| Friends (list, add, remove) | Working | |
| Shared files (list, rename, delete, unshare) | Working | |
| Kad (contacts, status, bootstrap, disconnect) | Working | |
| Search (start, get results, mark spam) | Working | Returns searchID, results via forEachResult |
| Uploads | Working | GetUploads handler implemented |
| Preferences (get/set) | Working | |
| Client shared files | Working | RequestClientSharedFiles + PushClientSharedFiles |
| Reset stats | Working | |
| Subscription mask | Stubbed | `handleSubscribe()` stores mask, but `broadcast()` ignores it |

#### 31c: Core Session (`src/core/app/`)

- [x] `CoreSession` class — `core/app/CoreSession.h/.cpp` (timer-driven process orchestrator, owns KnownFileList, SharedFileList, UploadQueue, UploadBandwidthThrottler, UploadDiskIOThread, Kademlia; 100ms timer drives `process()` on managers)
- [x] `AppContext` global context — `core/app/AppContext.h` (theApp singleton providing access to all managers)
- [x] Core as standalone library — `src/core/CMakeLists.txt` producing `libemulecore` (17 sub-modules, links Qt6::Core + Qt6::Network + Qt6::Multimedia + Qt6::HttpServer + OpenSSL + zlib + miniupnpc + yaml-cpp + libarchive)

#### 31d: GUI IPC Integration

- [x] `IpcClient` — `gui/app/IpcClient.h/.cpp` (connects to daemon, handshake protocol, request/callback with seqId, push event dispatch via Qt signals)
- [x] Auto-launch daemon from GUI — `gui/app/main.cpp` (resolves daemon binary path, connects on startup, retries after 1.5s if daemon launch needed)
- [x] GUI directory structure — `src/gui/` with sub-modules: `app/`, `panels/`, `controls/`, `dialogs/`, `graphics/`
- [x] CMake target `emuleqt` — `src/gui/CMakeLists.txt` (links eMule::Core + eMule::Ipc + Qt6::Widgets)
- [ ] Remove direct core object access from GUI panels — ServerPanel still uses direct `ServerList*`/`ServerConnect*` pointers alongside IPC; migrate to IPC-only
- [ ] Implement subscription filtering — `handleSubscribe()` stores `m_subscriptionMask` but `IpcServer::broadcast()` still sends to all handshaked clients without filtering

#### 31e: Remaining IPC Work

- [ ] Implement rich push event payloads — most push messages are bare notifications (e.g. `PushDownloadAdded` has no data); `PushServerState` has a CBOR map; add full entity snapshots to others
- [x] Complete `SetPreferences` handler — `applyPreferenceA()`/`applyPreferenceB()` handle 140 keys out of ~240 getters; covers all OptionsDialog-settable prefs (connection, server, Kad, directories, web server, security, statistics, display, notifications, IRC)
- [x] Remote daemon connection — `IpcClient::connectToDaemon(host, port)` supports hostname/IP; `CoreConnectDialog` provides host:port:token input when no local daemon found
- [ ] IPC authentication — `CoreConnectDialog` has a `token()` field but daemon-side verification not implemented
- [x] IPC reconnection — `IpcClient` has full auto-reconnect: `onConnectionLost()` → `scheduleReconnect()` with exponential backoff (1s → 30s max), `m_reconnectTimer`, auto re-handshake on reconnect

---

## Recommended Porting Order

The modules are ported bottom-up. Phases 1–6 are **complete**; Phase 7 is **well underway** with all panels, key controls/models, and several dialogs implemented. Remaining work is mostly minor controls, remaining dialogs, and polish.

| Phase | Modules | Status | Rationale |
|-------|---------|--------|-----------|
| **Phase 1** | 1, 2, 3, 29 | **Done** | Build system + platform abstraction + utilities + test infrastructure |
| **Phase 2** | 4, 7, 29c, 29f | **Done** | Crypto + protocol + their unit tests |
| **Phase 3** | 5, 6, 29d, 29e | **Done** | Networking layer + tests |
| **Phase 4** | 8, 9, 10, 11, 29g–29j | **Done** | Server, Client, Kademlia, Files + tests |
| **Phase 5** | 12–19, 29k–29r | **Done** | Queues, Search, IP filter, Stats, Prefs, Chat, Friends, Web + tests |
| **Phase 6** | 31, 29s2 | **Done** | IPC layer, daemon, GUI/core separation + IPC tests |
| **Phase 7** | 21–25, 29t | **Mostly done** | All 8 panels done, 10 dialogs done, 16 controls/models done; remaining: minor controls, a few dialogs, graphics utilities, widget tests |
| **Phase 8** | 20, 26, 27, 28, 29s, 29u | **Partially done** | Media (done), Localization (9 languages done, lupdate/lrelease configured), Resources (mostly done), Dependencies (done) |
| **Phase 9** | 29v, 30 | **Mostly done** | 8 integration/E2E tests done, legacy MFC files removed; `.vcxproj`/`.sln`/`.rc` removal pending |

**Testing philosophy:** Write tests for each module immediately after porting it (same phase).
Module 29v integration tests run after all modules are ported.

---

## Key MFC → Qt Mapping Reference

| MFC Class | Qt Replacement |
|-----------|---------------|
| `CWinApp` | `QApplication` |
| `CDialog` / `CWnd` | `QDialog` / `QWidget` / `QMainWindow` |
| `CListCtrl` | `QTreeView` + `QAbstractItemModel` |
| `CTreeCtrl` | `QTreeView` + `QAbstractItemModel` |
| `CEdit` | `QLineEdit` / `QTextEdit` |
| `CRichEditCtrl` | `QTextEdit` / `QTextBrowser` |
| `CComboBox` | `QComboBox` |
| `CButton` | `QPushButton` / `QCheckBox` / `QRadioButton` |
| `CProgressCtrl` | `QProgressBar` |
| `CStatusBar` | `QStatusBar` |
| `CToolBar` | `QToolBar` |
| `CTabCtrl` | `QTabWidget` |
| `CPropertyPage` | `QWidget` (as page in `QStackedWidget`) |
| `CPropertySheet` | `QDialog` + `QStackedWidget`/`QTabWidget` |
| `CSplitterWnd` | `QSplitter` |
| `CMenu` | `QMenu` / `QMenuBar` |
| `CString` | `QString` |
| `CFile` | `QFile` |
| `CArchive` | `QDataStream` |
| `CSocket` / `CAsyncSocket` | `QTcpSocket` / `QUdpSocket` |
| `CWinThread` | `QThread` |
| `CCriticalSection` | `QMutex` / `std::mutex` |
| `CEvent` | `QWaitCondition` / `std::condition_variable` |
| `CDC` / `CPaintDC` | `QPainter` |
| `CBitmap` | `QPixmap` / `QImage` |
| `CImageList` | `QIcon` list or custom container |
| `CToolTipCtrl` | `QToolTip` |
| `CTime` / `CTimeSpan` | `QDateTime` / `std::chrono` |

---

## Statistics

### Original MFC codebase
- **Total source files:** ~465 (250 unique base modules)
- **Kademlia subsystem:** 48 files across 6 sub-modules
- **Files with MFC classes:** ~103
- **Language/localization files:** 131
- **Resource files (icons, bitmaps):** 271+
- **Largest files:** `BaseClient.cpp` (107KB), `WebServer.cpp` (172KB)

### Ported Qt codebase (current)
- **Ported source files:** 333 (`.cpp` + `.h` in `src/`)
  - `src/core/`: ~224 files (17 sub-modules + webserver)
  - `src/ipc/`: 11 files (protocol, message, connection, serializers)
  - `src/daemon/`: 17 files (DaemonApp, IpcServer, IpcClientHandler, CoreNotifierBridge, CliIpcClient, CommandLineExec)
  - `src/gui/`: 81 files (MainWindow, 8 panels, 16 models/controls/delegates, 10 dialogs, 9 app utilities, IpcClient)
- **Test files:** 107 (`tst_*.cpp`)
- **Build targets:** 4 (`libemulecore`, `libemuleipc`, `emulecored`, `emuleqt`)
- **Porting modules:** 31 (including 23 test sub-modules)
- **Porting phases:** 9
- **Core modules (1–20, 28, 31):** Phases 1–6 complete
- **GUI (Modules 21–25):** Phase 7 mostly complete — all 8 panels, 10 dialogs, 16 controls/models implemented
- **Remaining:** Minor GUI controls (Module 23: 6 items), remaining dialogs (Module 24: 8 items), keyboard shortcuts (Module 27), remaining translations (122 of 131 languages), widget tests (29t), IPC polish (31d/31e), `.vcxproj`/`.sln`/`.rc` removal
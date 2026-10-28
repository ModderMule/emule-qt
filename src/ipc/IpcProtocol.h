#pragma once

/// @file IpcProtocol.h
/// @brief IPC wire protocol: message types, framing constants, encode/decode.
///
/// Every IPC message is a length-prefixed CBOR payload:
///   [4 bytes: big-endian uint32 payload length][CBOR payload]
///
/// The CBOR payload is always a QCborArray:
///   [ msgType: int, seqId: int, ...fields ]

#include <QByteArray>
#include <QCborArray>

#include <cstdint>
#include <optional>

namespace eMule::Ipc {

// ---------------------------------------------------------------------------
// Message type enumeration
// ---------------------------------------------------------------------------

/// What a PushNotifierEvent reports (MFC ShowNotifier's TBN_* cases that the core
/// raises). The text field carries the one variable part.
enum class NotifierEvent : int {
    DownloadFinished = 0,   ///< text: file name (MFC PartFile.cpp:3021)
    ConnectionLost   = 1,   ///< text: server name; not sent for a manual disconnect
    OutOfDiskSpace   = 2,   ///< text: file name (MFC PartFile.cpp:4292)
    PortBindFailed   = 3,   ///< text: port number (MFC emuleDlg.cpp:718-730)
};

enum class IpcMsgType : int {
    // -- Requests (GUI -> Core) -----------------------------------------------

    Handshake            = 100,  ///< [version: string]
    Ping                 = 101,  ///< [] — keepalive probe; daemon responds with Result(true)
    GetDownloads         = 110,
    GetDownload          = 111,  ///< [hash: string]
    PauseDownload        = 112,  ///< [hash: string]
    ResumeDownload       = 113,  ///< [hash: string]
    CancelDownload       = 114,  ///< [hash: string]
    SetDownloadPriority  = 115,  ///< [hash, priority, isAuto]
    ClearCompleted       = 116,  ///< [hashes?: array] — remove completed downloads (all, or just these)
    GetDownloadSources   = 117,  ///< [hash: string] — source clients for one download
    RenameDownload       = 118,  ///< [hash: string, newName: string] — rename an unfinished download
    RenameFiles          = 119,  ///< [[hash, newName]...] → [[hash, ok, error]...] — Mass Rename
    GetUploads           = 120,
    GetDownloadClients   = 121,  ///< [] — source clients we are downloading from
    GetKnownClients      = 122,  ///< [] — all known clients
    GetServers           = 130,
    // Server-keyed requests carry `addr` — the literal address — in addition to the
    // legacy numeric `ip`, which is 0 for an IPv6 server. The daemon prefers `addr`
    // when present and falls back to `ip`, so an older GUI still works.
    RemoveServer         = 131,  ///< [ip: int64, port: int64, addr: string]
    RemoveAllServers     = 132,  ///< []
    SetServerPriority    = 133,  ///< [ip: int64, port: int64, priority: int, addr: string]
    SetServerStatic      = 134,  ///< [ip: int64, port: int64, isStatic: bool, addr: string]
    AddServer            = 135,  ///< [address: string, port: int64, name: string, addr6?: string] — addr6 = other-family address of a dual-stack server
    SetServerOrder       = 136,  ///< [CborArray of [ip:int64, port:int64, addr:string]] (#24)
    SetServerEnabled     = 137,  ///< [ip: int64, port: int64, enabled: bool, addr: string] — lift or set the "disabled after too many failures" mark
    GetConnection        = 140,
    ConnectToServer      = 141,  ///< [] or [ip: int64, port: int64, addr: string]
    DisconnectFromServer = 142,
    ReleaseConnectHold   = 143,  ///< [] — first start wizard closed: do the auto-connect a `--hold-connect` daemon deferred
    StartSearch          = 150,  ///< [expression, fileType, method, minSize, maxSize, avail, ext, completeSrc]
    GetSearchResults     = 151,  ///< [searchID, since?: int64] — since = the stamp of the last answer: only rows changed after it. Answer: [ok, rows, stamp, total]
    StopSearch           = 152,  ///< [searchID: int]
    RemoveSearch         = 153,  ///< [searchID: int]
    ClearAllSearches     = 154,  ///< []
    DownloadSearchFile   = 155,  ///< [hash: string, fileName: string, fileSize: int64, link: string, category: int, searchID: int, paused: bool, row: map] — link wins if set; category on are optional, paused absent or null = the option decides, row = SearchResultRow::fileRef() of a restored tab (searchID 0)
    /// [searchID: int] → bool. Next page of a finished Usenet / torrent (Server)
    /// search whose PushSearchState said hasMore; false when it has none.
    SearchMore           = 157,
    GetKnownTypes        = 156,  ///< [hashes: QCborArray of strings] → [types: QCborArray of ints]
    /// [afterHash?, limit?] -> {files: [row], more, total}. Rows in hash order behind
    /// afterHash; no fields = all rows in one reply (may not fit a frame for a large share).
    GetSharedFiles       = 160,
    SetSharedFilePriority = 161, ///< [hash: string, priority: int, isAuto: bool]
    ReloadSharedFiles    = 162, ///< [rebuildMetaData?] — rescan shared directories, or re-read media tags
    GetEd2kLink          = 163, ///< [hashes: QCborArray of strings, hashset: bool,
                                ///<  sourceHint: bool, html: bool, mediaInfo?: bool]
                                ///<   mediaInfo (optional, html only): a media line
                                ///<   under each complete file's link
                                ///<   → [ok: bool, [links: QCborArray of strings,
                                ///<                 sourceHintAvailable: bool]]
                                ///<   links[i] pairs with hashes[i]; a hash that no longer
                                ///<   resolves yields an empty string, never an error.
    GetFriends           = 170,
    AddFriend            = 171,  ///< [hash, name, ip, port, addr: string] — addr wins if set
    RemoveFriend         = 172,  ///< [hash, addr: string, port] — addr+port when hash empty
    SendChatMessage      = 173,  ///< [hash: string, message: string]
    SetFriendSlot        = 174,  ///< [hash: string, enabled: bool]
    EndChatSession       = 175,  ///< [hash: string] — the chat window closed, so the
                                ///<   peer goes back to ChatState::None and a dial we
                                ///<   started for it is cancelled. MFC does this from
                                ///<   CChatSelector::EndSession (ChatSelector.cpp:464).
    /// {...StatsSnapshot, streamToken, maxDownloadKb, usenetLimitKb, ed2kBudgetKb}.
    /// The last three are the live download split, effective caps in KB/s with
    /// every sentinel resolved: 0 only when maxDownloadKb is 0 (unlimited), and a
    /// cap below maxDownloadKb means the other engine is busy.
    GetStats             = 180,
    GetPreferences       = 190,
    SetPreferences       = 191,  ///< [key, value, ...]
    Subscribe            = 200,  ///< [eventMask: int]
    GetKadContacts       = 210,
    GetKadStatus         = 211,
    BootstrapKad         = 212,  ///< [ip: string, port: int]  (empty = from nodes.dat)
    DisconnectKad        = 213,
    SyncLogs             = 214,  ///< [lastLogId: int64]  — request buffered logs since ID
    Shutdown             = 215,  ///< [restart?: bool] — graceful daemon shutdown; restart = start again with the same arguments
    GetKadSearches       = 216,
    GetKadLookupHistory  = 217,  ///< [searchId: int] — lookup history for a search
    GetNetworkInfo       = 218,  ///< [] — all network info for the Network Information dialog
    RecheckFirewall      = 219,  ///< [] — restart TCP + UDP firewall checks
    ReloadIPFilter       = 220,  ///< [] — reload IP filter from ipfilter.dat
    GetSchedules         = 221,  ///< [] — returns schedulerEnabled + full schedule list
    GetNetworkInterfaces = 223,  ///< [] → CborArray of {name, friendlyName, index, addresses[]} — the daemon's active interfaces, for the bind selection
    /// [network: int (1 BitTorrent, 2 ed3k)] → [ok, {network, tcpPort?, udpPort,
    /// targets: [{family: 4|6, ip, tcp?: {open, reason}, udp?: {open, reason}}]}] or an
    /// Error [PortTestApi::Failure, English detail].
    /// Runs the emule-qt.org port test from the daemon (PortTestRunner); takes up to ~20 s.
    TestPorts            = 224,
    SaveSchedules        = 222,  ///< [enabled: bool, schedules: CborArray] — replace all
    ScanImportFolder     = 230,  ///< [folder: string, removeSource: bool] → scan + queue + return jobs
    GetConvertJobs       = 231,  ///< [] → current job list with statuses
    RemoveConvertJob     = 232,  ///< [index: int] → remove non-in-progress job
    RetryConvertJob      = 233,  ///< [index: int] → re-queue a failed job

    StopDownload         = 240,  ///< [hash: string] — stop (not pause) a download
    OpenDownloadFile     = 241,  ///< [hash: string] — open completed file on daemon
    OpenDownloadFolder   = 242,  ///< [hash: string] — open folder containing file on daemon
    /// [searchID: int, hash: string, isSpam: bool = true] — mark/unmark a search
    /// result as spam. Field 2 is optional so older senders keep marking as spam.
    MarkSearchSpam       = 243,
    ResetStats           = 244,  ///< [] — reset session statistics
    RenameSharedFile     = 245,  ///< [hash: string, newName: string]
    DeleteSharedFile     = 246,  ///< [hash: string] — delete file from disk + shared list
    UnshareFile          = 247,  ///< [hash: string] — remove from shared list (keep on disk)
    SetDownloadCategory  = 248,  ///< [hash: string, category: int]
    GetDownloadDetails   = 249,  ///< [hash: string] → extended info (filePath, comments)
    PreviewDownload      = 250,  ///< [hash: string] — preview partial file on daemon
    RequestClientSharedFiles = 251, ///< [clientHash: string] — ask client for shared list
    GetClientDetails     = 252, ///< [clientHash: string] → extended client info for detail dialog
    GetSharedFileDetails = 253, ///< [hash: string] → extended shared file info for detail dialog
    GetServerState       = 254, ///< [] → connected/connecting/firewalled/clientID/serverId
    SearchKadNotes       = 255, ///< [hash: string, fileName: string] — trigger Kad notes lookup for file

    GetCollectionInfo       = 256, ///< [hash: string] → collection metadata for shared file
    SaveCollection          = 257, ///< [name, fileHashes[], textFormat, sign, overwrite] → create & share; error "exists" unless overwrite
    // 258 was SearchAuthorCollections; the GUI now drives that search through StartSearch.
    GetServerMessages       = 259, ///< [fromId] → CborArray of [id, type, text] — Server Info backlog
    /// [searchID: int, hash: string] → comments/tags for one search result.
    /// The searchID is required because a hash is only unique within a search tab.
    GetSearchResultDetails  = 260,
    /// [searchID: int, hash: string] — ask the peer behind a browsed file for preview
    /// frames. The frames arrive later as PushSearchPreview.
    RequestSearchPreview    = 276,

    /// [] -> [ok, map]. The Kademlia branch of the Statistics window; its own
    /// request for the same reason GetUsenetStats is.
    /// { session: KadCounters, cumulative: KadCounters,
    ///   current: { running, connected, firewalled, udpFirewalled, lanMode,
    ///              contacts, verified, bootstrap, byType: [5 counts],
    ///              byVersion: [[kadVersion, count]], users, files,
    ///              indexedKeywords, indexedSources, indexedNotes, indexedLoad,
    ///              activeSearches, safeKadTracked, safeKadBanned },
    ///   seen: { session | cumulative: { contacted, listed (distinct node IDs,
    ///           estimated), countries: [[cc, nodes]] most first, cc "" = unknown } } }
    GetKadStats             = 277,

    /// [] -> [ok, map]. Distinct eD2K clients by user hash, for the Clients
    /// branch of the Statistics window.
    /// { seen: { session | cumulative: { seen, identified (SecureIdent passed;
    ///           both estimated), countries: [[cc, clients]] most first,
    ///           cc "" = unknown } } }
    GetClientStats          = 278,

    /// [] → re-read the web server template from disk. For "same path, edited
    /// content"; a changed templatePath is a config change and already goes
    /// through SetPreferences → restartWebServer().
    ReloadWebTemplate       = 272,

    /// [] → {loaded, buildDate (secs), lastCheck (secs), updating} — GeoLite2 state
    /// for the Options page.
    GetGeoIpStatus          = 273,
    /// [] → [ok: bool, message: string, status: map as GetGeoIpStatus]. Downloads
    /// GeoLite2-Country now with the stored MaxMind credentials; answers when done.
    UpdateGeoIpDatabase     = 274,
    /// [hosts: string[]] → {host: cc}. Resolves each hostname daemon-side and maps
    /// the address to a country; "" when unknown. Used by the news-server list.
    LookupHostCountries     = 275,

    /// [fromSeq: int] -> {epoch, oldestSeq, samples: [[seq, down, up], ...]}
    /// Sample history for the toolbar download/upload graph. Core samples once a
    /// second and every GUI replays from its own seq, so a restarted GUI comes back
    /// to a full trace and two GUIs draw the same one.
    GetSpeedHistory         = 261,
    /// [fromSeq: int] -> {epoch, intervalSec, oldestSeq, samples: [[seq, ts, ...], ...]}
    /// Same replay for the three statistics graphs; see StatsGraphSample for the
    /// field order inside each sample.
    GetStatsHistory         = 262,
    /// [] — put the cumulative counters back to the values ResetStats saved.
    /// The current values become the new backup, so sending it twice undoes the
    /// restore (MFC: srchybrid/StatisticsTree.cpp:175).
    RestoreStats            = 263,

    /// [baseUrl: string, secret: string] -> {ok, error, service, version,
    ///   implementation, uploadRequiresAuth, maxChunkSize, currentBaseUrl, unchanged}
    /// GET <baseUrl>/v1/info, so a config link can be shown to the user only once
    /// the endpoint has identified itself as a cache. The secret never leaves the
    /// daemon: it is compared against the stored key to answer `unchanged`, and
    /// the probe request itself carries no credential at all.
    ProbeHttpCacheServer    = 264,
    /// [baseUrl: string, secret: string] -> [ok: bool, error: string]
    /// Store an HTTP Cache configuration from an ed2k://|httpcache| link. Probes
    /// again before writing — the caller having probed is not a reason to skip it,
    /// and the CLI path never probes at all.
    ApplyHttpCacheConfig    = 265,

    /// [path: string, shared: bool] -> [ok: bool, error: string]
    /// Share or unshare one file by path. Unsharing records the path durably, so a
    /// reload or a restart does not put the file back — the in-memory hash set never
    /// did that (MFC CSharedFileList::ExcludeFile / AddSingleSharedFile).
    SetFileShared           = 266,
    /// [dirPath: string] -> [{name, path, size, shared, canToggle, hash}]
    /// List one directory's files with their share state, so the "All Directories"
    /// tree can show unshared files alongside shared ones and offer a checkbox.
    /// `canToggle` is false where the state is forced — the incoming directory is
    /// always shared, a non-shareable directory never is.
    BrowseDirectory         = 267,

    // -- Download categories (268-270) ---------------------------------------
    //
    // Their own opcodes rather than keys in Get/SetPreferences, for the reason
    // the news-server and indexer lists have theirs: a list of maps whose
    // *order is its identity* needs a contract of its own. A category's index
    // is what part.met stores, so a set that silently reordered would
    // re-file every download in the queue.

    /// [] -> [[{index, title, incoming, resolvedIncoming, comment, autocat,
    ///          autocatRegexp, color, prio}]]
    /// Index 0 first and always present. `resolvedIncoming` is what
    /// incomingDirForCategory() would answer — the GUI shows the effective
    /// folder, which for index 0 and for an unset category is the global one.
    GetCategories           = 268,
    /// [[{...same shape, minus resolvedIncoming, plus oldIndex...}]]
    ///     -> [ok: bool, error: string]
    /// Replaces the whole list; entry 0 is forced to exist.
    ///
    /// `oldIndex` is the entry's `index` from the last GetCategories, or absent
    /// for one the user just created. It is how the daemon moves each
    /// download's category along with the category itself: position alone
    /// cannot distinguish "category 2 was deleted" from "category 2 moved to
    /// slot 3", and guessing wrong silently re-files live downloads.
    /// A changed folder also re-scans the share.
    SetCategories           = 269,
    /// [category: int, action: int] -> [ok: bool]
    /// Bulk pause/resume/stop/cancel over one category, plus "resume next".
    /// See Ipc::CategoryAction.
    SetCategoryStatus       = 270,

    /// [hash: string, comment: string, rating: int] -> [ok: bool]
    /// Post the local user's own comment and rating for one file — the write half of
    /// MFC's CCommentDialog::OnApply (srchybrid/CommentDialog.cpp:166-184). An empty
    /// comment clears it; rating 0 means "not rated", 1 means fake, 2-5 poor..excellent.
    ///
    /// The file must be in the shared list. eMule never publishes a comment for a file
    /// it is not sharing, which is exactly why the original greys its whole comment page
    /// out for those (CommentDialog.cpp:117-121) rather than letting the user type into
    /// a void. Part files are shared files here, so a download qualifies.
    ///
    /// The daemon persists both to fileinfo.ini, clears the Kad notes republish timer so
    /// the next STORENOTES carries them, and marks every peer we are uploading to so its
    /// next OP_FILEDESC does too. Nothing comes back but `ok`: your own comment never
    /// appears in the `comments[]` of a details reply, which is other people's.
    SetFileComment          = 271,

    /// [userHash: string] → lift a ban on that client (MFC MP_UNBAN).
    UnbanClient             = 279,

    // -- Download menu, advanced entries (MFC DownloadListCtrl) ---------------
    /// [hash: string, url: bool, text: string, port: int] → ok when the source was
    /// taken. A peer is `text` = address (or "address:port") + `port`; a URL source
    /// is `text` alone.
    AddDownloadSource       = 280,
    /// [hashes: string[], limit: int] — the file's own source limit, 0 = global.
    SetDownloadSourceLimit  = 281,
    /// [hashes: string[], previewPrio: int, pauseOnPreview: int] — each -1 leave,
    /// 0 off, 1 on (MFC "Increase priority for preview parts" / "Pause when
    /// preview is possible").
    SetDownloadPreviewFlags = 282,
    /// [hash: string, path: string] — import the parts of a file on the daemon's
    /// host that match this download's hashes. Runs in the background; the row's
    /// fileOp reads "importing" meanwhile. An empty path stops a running import.
    ImportDownloadParts     = 283,
    /// [hash: string] → [ok, partFilePath: string]. Writes buffered data out, for a
    /// preview application that reads the part file itself (MFC ExecutePartFile).
    FlushDownload           = 284,
    /// [url: string] → Result when done. The daemon fetches a nodes.dat and reads it
    /// in, also while Kad is running (MFC CKademliaWnd::UpdateNodesDatFromURL).
    ImportKadNodes          = 285,
    /// [] → [ok, {sharedDirs: [string], singleSharedDirs: [string]}]. What the folder
    /// tree marks shared folders from: the shared directories, and the folders that
    /// hold an individually shared file (each once, however many files).
    GetSharedDirState       = 286,
    /// [searchID: int, hash: string, name: string] — drop a result from the daemon's
    /// list too; an empty name is the file with all its names, else that one name row.
    RemoveSearchResult      = 287,

    /// [offset: int] -> [ok, {files: [{hash, fileName, fileSize}], next: int, more: bool}].
    /// Every file known.met remembers, shared or not, a page at a time — the
    /// "Known" side of the collection editor (MFC CopyKnownFileMap).
    GetKnownFiles           = 288,

    /// [hash] -> [ok, path]. Rebuild a readable archive from what a download has so
    /// far, into a file of its own; the download is only read (MFC "Create preview
    /// file", CArchiveRecovery::recover).
    RecoverArchivePreview   = 289,

    /// [hash: string] -> [ok, swapped: int]. Moves the download's idle A4AF sources
    /// to it (Source Handling > Swap All A4AF Sources to This File).
    SwapA4AFToFile          = 290,

    /// [url: string] -> [ok, [success: bool, entries: int, message: string]] when done.
    /// The daemon downloads ipfilter.dat (empty url = the configured one), saves and
    /// reloads it. Security page "Load" button.
    UpdateIpFilterNow       = 291,

    /// [] -> [ok, queued: int]. Hash every shared file again and rebuild known.met
    /// when all are done; queued -1 = a pass is running already. Files page button.
    RehashAllSharedFiles    = 292,

    // -- Indexers (700-719) --------------------------------------------------
    //
    // The shared newznab/torznab client, reserved here when the Usenet blocks
    // were allocated. It belongs to neither network: newznab (Usenet) and
    // torznab (BitTorrent) are the same API with a different XML attribute
    // namespace, and Prowlarr and NZBHydra2 serve both from one endpoint.
    //
    // StartSearch = 150 is deliberately not reused. Its payload is ED2K-shaped
    // (fileType, minSize, avail, completeSrc) and the daemon would have to guess
    // which network a request meant.

    /// [] -> [{name, url, kind, enabled, hasApiKey, capsProbedAt, capsOk}]
    /// The API key is never sent to the GUI — only whether one is stored. Same
    /// rule as GetNewsServers, and for the same reason: a key that never reaches
    /// the GUI cannot leak through a screenshot, a log, or an IPC session on a
    /// non-loopback socket. It also rides in every request URL, so it is the one
    /// secret here that is easy to spill by accident.
    GetIndexers             = 700,
    /// [[{...same shape, plus optional `apiKey`...}]] -> [ok: bool, error: string]
    /// Replaces the whole list. An entry that omits `apiKey` keeps the stored
    /// one, which is what lets the Options page round-trip a list whose secrets
    /// it was never given.
    SetIndexers             = 701,
    /// [{url, apiKey, kind}] -> [ok, response, error]
    /// Runs t=caps and reports the indexer's **own** error text. "Incorrect user
    /// credentials" tells a user which field to fix where "Unauthorized" does
    /// not — the same judgement TestNewsServer makes with its status line.
    TestIndexer             = 702,
    /// [name: string] -> [{limitMax, modes: [{name, available, params}],
    ///                     categories: [{id, name, subcats}]}]
    /// The cached capabilities, so the search form can grey out what this
    /// indexer does not advertise instead of sending a query it will reject.
    GetIndexerCaps          = 703,
    /// [query, cat: [int], mode, indexers: [string]] -> [ok, searchId | error]
    /// searchId is 0 with an error when nothing is configured — which is worth
    /// saying, because an empty result list reads as "nothing matched".
    StartIndexerSearch      = 704,
    /// [searchId: int] -> [ok]
    StopIndexerSearch       = 705,
    /// [searchId: int] -> [ok]
    RemoveIndexerSearch     = 706,
    /// [searchId: int, resultId: string, force: bool, category: int,
    ///  priority: int, paused: bool] -> [ok, itemIdOrError]
    /// The daemon fetches the .nzb itself and hands it to the Usenet queue. The
    /// download URL carries the API key, so it never travels to the GUI and the
    /// GUI never issues the request.
    ///
    /// Fields 3-5 are AddNzb's, and absent read the same way: 0 means "nobody
    /// chose a category", so a grab with no opinion still gets
    /// auto-categorisation.
    GrabIndexerResult       = 707,

    /// [] -> [[{name, kind, enabled, query, categories, indexers, url, hasUrl,
    ///          accept, reject, minSize, maxSize, maxAgeDays, intervalMinutes,
    ///          grabExisting, lastPolled, lastError, lastMatched, seenCount,
    ///          polling}]]
    /// `url` arrives **redacted** and `hasUrl` says whether one is stored. A
    /// pasted RSS link carries the API key in its query, so it is a credential
    /// wearing a URL's clothes and gets the GetIndexers treatment.
    GetIndexerFeeds         = 708,
    /// [[{...same shape, plus optional `url`...}]] -> [ok: bool, error: string]
    /// Replaces the whole list. An entry that omits `url` keeps the stored one,
    /// which is what lets the Options page round-trip a feed whose URL it was
    /// only ever shown redacted.
    SetIndexerFeeds         = 709,
    /// [name: string] -> [ok, error]
    /// Check one feed now, or every enabled feed when the name is empty. Even
    /// then they go one per tick rather than at once: a "check everything" click
    /// must not open one request per feed at the same instant.
    PollIndexerFeedNow      = 710,

    // -- Usenet (720-799) ----------------------------------------------------
    //
    // A block, not the next free integer. The core request space runs 100-299
    // and the response block starts at 300, leaving roughly thirty slots; a
    // second network taking them would leave ED2K none. IpcMsgType is a plain
    // int over CBOR, so the space above 600 costs nothing. 700-719 is reserved
    // for the shared newznab/torznab indexer client, which Usenet and a future
    // BitTorrent module both use.

    /// [] -> [{name, host, port, tls, user, hasPassword, level, group, optional,
    ///         retention, joinGroup, maxConnections, certVerification, enabled}]
    /// The password is never sent to the GUI — only whether one is stored. A
    /// GUI that never holds the secret cannot leak it into a screenshot, a log,
    /// or an unencrypted IPC session on a non-loopback socket.
    GetNewsServers          = 720,
    /// [[{...same shape, plus optional `password`...}]] -> [ok: bool, error: string]
    /// Replaces the whole list. An entry that omits `password` keeps the one
    /// already stored, which is what lets the GUI round-trip a list it was never
    /// given the secrets for.
    SetNewsServers          = 721,
    /// [{host, port, tls, user, password, certVerification}] -> [ok, response, error]
    /// Connect, authenticate and disconnect. `response` carries the provider's
    /// literal status line, because "281 Authentication accepted" tells a user
    /// far more than a green tick.
    TestNewsServer          = 722,

    /// [] -> [{id, name, status, statusText, priority, percent, totalBytes,
    ///         decodedBytes, segmentCount, doneSegments, missingSegments, error,
    ///         files: [{name, size, percent, finalPath, isPar2, missingSegments,
    ///                   index, previewable, previewNote}]}]
    /// `previewable` says a Preview action is worth offering for that file, and
    /// `index` is what the preview URL addresses. The GUI cannot decide it: the
    /// answer needs the real post-yEnc filename, the contiguous-prefix length,
    /// and — since phase 6b — whether the file is a volume of a *stored* archive
    /// set holding a playable file. Every volume of such a set reports true,
    /// because they all describe the same set; *which* file inside it gets
    /// played is chosen separately, by ListUsenetArchiveEntries and the preview
    /// URL's `entry=`. With no choice made it is the first playable file, so a
    /// release that packs an `.nfo` ahead of the feature plays the feature.
    /// `previewNote` is why not, when there is something to say: a compressed,
    /// solid or encrypted archive can never be streamed, and the GUI shows the
    /// sentence rather than leaving an unexplained greyed-out menu entry.
    /// The whole queue. The GUI polls this; individual changes arrive as
    /// PushUsenetQueueItem, which carries one item in the same shape.
    GetUsenetQueue          = 723,
    /// [nzbBytes: bytes, name: string, automatic: bool] -> [ok, idOrError]
    /// The GUI sends the file's contents rather than a path: the daemon may be on
    /// another machine, and a path that resolves on one would silently open the
    /// wrong file — or nothing — on the other.
    ///
    /// `automatic` decides how the caller treats an "already downloaded" answer,
    /// and whether autoAddPaused applies. Absent reads as false, which is what
    /// every GUI caller is — the field exists for the watch folder and for feeds.
    ///
    /// Field 3 `force` re-sends an add the user was asked about and said yes to.
    /// It suppresses the "already downloaded" refusal and nothing else: a release
    /// still arriving is refused whatever this says. Same probe-ask-resend shape
    /// as ProbeHttpCacheServer followed by ApplyHttpCacheConfig, and it keeps no
    /// daemon-side state between the two.
    ///
    /// The reply's field 2 is a UsenetAddOutcome int. Without it a caller has to
    /// tell "we already have this" from "that broke" by reading the sentence,
    /// which stops working the first time one is reworded or translated.
    ///
    /// Field 4 `password` is an archive passphrase the user typed in the Add NZB
    /// dialog. A manual add's password wins over the NZB's own
    /// `<meta type="password">`, because a person typed it; see
    /// UsenetQueue::addNzb() for the full precedence.
    ///
    /// Fields 5-7 are what the Add NZB dialog now asks for: `category` (an index
    /// into the category list, 0 meaning "nobody chose", which is what lets
    /// auto-categorisation run), `priority` (-2..+2, clamped by the daemon) and
    /// `paused`. All three absent read as 0/0/false, which is exactly what every
    /// older GUI sends and exactly what this did before they existed.
    AddNzb                  = 724,
    /// [id: string, deleteFiles: bool] -> [ok]
    RemoveUsenetItem        = 725,
    /// [id: string] -> [ok]
    PauseUsenetItem         = 726,
    /// [id: string] -> [ok]
    ResumeUsenetItem        = 727,
    /// [id: string, priority: int] -> [ok]
    SetUsenetItemPriority   = 728,

    /// [id: string, fileIndex: int] -> [{status, note, entries: [{entry, name,
    ///                                   size, playable, note}]}]
    /// The files *inside* an archive set, so the GUI can offer a choice when a
    /// release holds more than one playable file — a season pack, or a feature
    /// beside its `.nfo`. `entry` is the ordinal the preview URL's `entry=`
    /// takes; it is sent explicitly and never inferred from array position,
    /// because a partial and a finished scan must agree on it.
    ///
    /// **This one fetches.** The header of the second file inside a set sits
    /// past the first file's payload, usually in a later volume, so listing
    /// costs articles where `previewable` above costs nothing. `status` is
    /// therefore a state machine the GUI polls: 0 Unknown (nothing on disk and
    /// nothing being fetched — paused or gone), 1 Scanning (ask again; entries
    /// grows), 2 Complete, 3 NotSeekable (solid or header-encrypted: there is
    /// nothing to list), 4 NotAnArchive (a raw post or a `.001` split — one
    /// file, and there was never a choice). Only 1 is non-terminal.
    ListUsenetArchiveEntries = 729,

    /// [url: string] -> [ok, idOrError]
    /// **This one fetches.** The daemon downloads the .nzb itself rather than
    /// having the GUI fetch it and post the bytes through AddNzb, for the reason
    /// GrabIndexerResult fetches daemon-side: the URL is often reachable only
    /// from the daemon's own network — a self-hosted indexer on the LAN it sits
    /// on — and the bytes have to end up in its queue regardless.
    ///
    /// Only http and https are accepted. QNetworkAccessManager also speaks
    /// file: and qrc:, and a client naming one of those would be asking the
    /// daemon to read its own disk. Size, timeout and the no-downgrade redirect
    /// policy are HttpFileDownload's, already.
    ///
    /// One URL per request. A paste of several is several requests, so a dead
    /// link costs its own line and nothing more, and no reply waits out another
    /// URL's timeout.
    ///
    /// Field 1 is the same `automatic` bit AddNzb takes, field 2 the same `force`,
    /// field 3 the same `password`, fields 4-6 the same `category`, `priority`
    /// and `paused`, and the reply carries the same outcome int.
    AddNzbUrl               = 730,

    /// [accountId: string, periodBytes: int64, totalBytes: int64] -> [ok, error]
    ///
    /// Correct one account's usage meter. -1 leaves a figure alone, so a plain
    /// reset is [id, 0, -1].
    ///
    /// It exists because the meter is *measured*, not reported: NNTP has no
    /// command that asks a provider what you have spent, so our figure is the
    /// application-level inbound byte count and reads a few percent under theirs.
    /// A user who switches plans, corrects a billing day or tops up a block
    /// account has no other way to make it true. The counter itself travels
    /// read-only on GetNewsServers, the way `hasPassword` does.
    SetNewsServerUsage      = 731,

    /// [itemId] -> [ok, error]. Re-run the availability probe for one queued
    /// release: ask the configured accounts, with STAT, whether they still hold
    /// it. A release queued a week ago is a different question from the one
    /// answered when it was added, and there is otherwise no way to ask again.
    ///
    /// The verdict itself needs no opcode — it rides the item map that
    /// GetUsenetQueue and PushUsenetQueueItem already carry, as postPercent and
    /// stalledReason do. `ok` false means the item is unknown or is in a state
    /// that cannot be probed; it never means the release is bad.
    CheckUsenetItem         = 732,

    /// [titles: QCborArray of strings] -> [ok, types: QCborArray of ints]
    ///
    /// What we already know about each indexer search row, so the Search panel
    /// can mark it and ask before re-downloading. The Usenet counterpart of
    /// GetKnownTypes = 156, and it reuses that enumeration's *numbering* so one
    /// colour helper serves both result models: 0 unknown, 2 in the Usenet queue
    /// right now, 3 downloaded, 4 cancelled. 1 (Shared) is never emitted — a
    /// Usenet release is not a shared file.
    ///
    /// Titles only, no sizes. An indexer's reported size is its own arithmetic
    /// over the NZB and disagrees with totalEncodedBytes() often enough that
    /// matching on it produces *false negatives* — no warning at all, which is
    /// the wrong way to be wrong. A false positive costs one dismissible
    /// question. What an add actually does is still decided by the article
    /// digest, which this cannot see.
    GetUsenetKnownTypes     = 733,

    /// [itemId] -> [ok, map]. Everything about one queued release that the queue
    /// row deliberately leaves out: per-file article counts, poster, date,
    /// newsgroups, the NZB's own subject and part counter, and the scratch and
    /// published paths.
    ///
    /// On-demand and never pushed. PushUsenetQueueItem fires every 250 ms per
    /// changing item, and a newsgroup list plus a per-file segment tally on each
    /// of those would be paying, continuously, for a dialog nobody has open. The
    /// details dialog polls this instead, and only while it is on screen.
    GetUsenetItemDetails    = 734,

    /// [itemId, password] -> [ok, error]. Set the archive passphrase for one
    /// queued release.
    ///
    /// **Write-only.** The password never travels the other way — GetUsenetQueue
    /// reports `hasPassword`, exactly as GetNewsServers reports it for provider
    /// accounts. An empty string clears it, which is how a user takes back a
    /// wrong guess.
    ///
    /// Setting it on a *failed* item retries it, because the whole release is
    /// already on disk and the password was the only thing missing. `ok` false
    /// means the item is unknown, never that the password is wrong — nothing
    /// knows that until the unpack runs.
    ///
    /// Numbered explicitly because the two above it are not: an opcode inserted
    /// anywhere in this block renumbers everything after it, and the GUI and the
    /// daemon then disagree about what 734 means.
    SetUsenetItemPassword   = 735,

    /// [itemId, category: int] -> [ok, error]. Move one queued release into a
    /// download category, or out of one with 0.
    ///
    /// The *index* travels, never a path: which folder that is gets resolved at
    /// completion, so a category repointed between queueing and landing does the
    /// right thing without anything being re-sent.
    SetUsenetItemCategory   = 736,

    /// [category: int, action: Ipc::CategoryAction] -> [ok, error]. Apply one
    /// action to every queued release in a category.
    ///
    /// Reuses the ED2K action enum rather than minting a Usenet-shaped twin, but
    /// only **Pause**, **Resume** and **Cancel** mean anything here: `Stop` is
    /// ED2K's "keep the file, drop the sources" and Usenet has no sources, and
    /// `ResumeNext` ranks paused files by the category's a4af priority, which is
    /// an ED2K concept. Both are refused with a reason rather than silently
    /// ignored — a GUI that sends one has a bug worth seeing.
    SetUsenetCategoryStatus = 737,
    /// [] -> [ok, map]. The Usenet branch of the Statistics window. A request of
    /// its own rather than GetStats keys, because GetStats is polled every second
    /// for the status bar whether or not the panel is open.
    ///
    /// Counter maps are keyed by the field walks in core/stats/NetworkCounters.h —
    /// decode them with countersFromCbor():
    /// { usenet: { session: UsenetCounters, cumulative: UsenetCounters,
    ///             current: { running, downRate (B/s wire), limitKb (0 = none),
    ///                        activeConnections, openConnections, paused,
    ///                        queue: { count, downloading, queued, paused, checking,
    ///                                 postProcessing, failed, complete,
    ///                                 totalBytes, downloadedBytes, leftBytes } },
    ///             servers: [ { accountId, name, host, enabled, openConnections,
    ///                          session: UsenetServerCounters,
    ///                          <GetNewsServers usage fields: quotaKind, quotaBytes,
    ///                           periodBytes, totalBytes, periodStart, resetsOn,
    ///                           overQuota> } ] },
    ///   indexer: { session: IndexerCounters, cumulative: IndexerCounters } }
    GetUsenetStats          = 738,

    /// [itemId, fileIndices: [int], skipped: bool] -> [ok, error]. Leave files of
    /// one release out of the download, or bring them back. The daemon widens a
    /// request to the whole archive set and refuses par2 files, every payload file
    /// at once, and releases already post-processing or complete.
    SetUsenetFilesSkipped   = 739,

    /// [paused: bool] -> [ok]. Pause the whole Usenet engine: nothing new starts,
    /// articles in flight finish, and no item's status changes. Persisted as
    /// `usenet.paused`; PushUsenetEngineState tells every client.
    SetUsenetPaused         = 740,

    /// [nzbBytes, name] -> [ok, {name, files: [{index, name, size, isPar2,
    /// setKey}]}] or [false, error]. Parse an .nzb without queueing it, so the
    /// Add NZB dialog can offer its files. `index` is what AddNzb's field 8 names;
    /// `setKey` groups the volumes of one archive set, empty for other files.
    InspectNzb              = 741,
    /// [] -> [ok, count]. Remove every Complete item from the list, files kept.
    /// The Usenet twin of ClearCompleted.
    ClearUsenetCompleted    = 742,

    // -- eNode meta search (750-754) -------------------------------------------
    //
    // Torrent/Usenet rows an eNode server blends into eD2K search answers. A row
    // is recognised by its meta hash (`metaKind` in GetSearchResults), never by
    // its name. The metafile comes from that server's Meta API, which may need
    // an account. Every failure carries a MetaStatus map as the LAST field:
    //   {status: MetaStatus, serverName, serverAddr: "addr:port", authMode,
    //    registrationUrl, accountUrl, msgCode, pendingSteps: [{title, kind, url}]}

    /// [searchID, hash, (row)] -> [true, {content: bytes, fileName, kind}] or
    /// [false, error, meta]. For saving the .torrent/.nzb in the GUI; capped
    /// below MaxPayloadSize.
    ///
    /// Optional `row` {name, metaKind, metaCatalogId, metaServers: [[ip, port]]}
    /// is used when the search is gone (a tab restored after a restart).
    FetchMetaFile           = 750,

    /// [searchID, hash, force, category, priority, paused, (row)] -> the AddNzb
    /// reply ([ok, itemId|error, outcome]) plus meta as field 3. Usenet rows
    /// only: the daemon fetches, verifies and queues the NZB. `row` as above.
    DownloadMetaResult      = 751,

    /// [serverAddr] -> [true, {meta keys + loggedIn, username, state,
    /// expiresAt}] or [false, error, meta]. AccountApi.GetAuthStatus.
    GetMetaAuthStatus       = 752,

    /// [serverAddr, username, password] -> [ok, error, meta + loggedIn,
    /// username, state]. AccountApi.Login; the daemon keeps the token, never
    /// the password.
    MetaLogin               = 753,

    /// [serverAddr] -> [ok, error]. AccountApi.Logout, then forget the token.
    MetaLogout              = 754,

    // -- BitTorrent (800-899) -------------------------------------------------
    //
    // The block of research doc §5.1. A torrent is named by its id, minted by the
    // daemon: "bt:v1:<40 hex>" or "bt:v2:<64 hex>" (src/bt/BtIds.h). Rows are the
    // CBOR maps of src/daemon/BtBridge.cpp. 503 = the BitTorrent module is not built
    // or not running.

    /// [] -> [{id, name, savePath, state, size, done, progress, rateDown, rateUp,
    ///         peers, seeds, peersTotal, seedsTotal, downloaded, uploaded,
    ///         failedBytes, error,
    ///         hashV1, hashV2, paused, hasMetadata, private, addedAt, completedAt,
    ///         seedingSeconds, queuePosition, pieceMap, pieces, pieceLength,
    ///         numFiles, eta, comment, creator, createdAt, lastActivity,
    ///         activeSeconds, haveBytes, totalSize, availability}]
    GetTorrents             = 800,
    /// [data: bytes, savePath: string (empty = BT download dir), paused: bool]
    /// -> {outcome, id, message}; outcome is BtAddOutcome (0 added, 1 duplicate,
    /// 2 invalid, 3 not running, 4 failed)
    AddTorrentFile          = 801,
    /// [uri: string, savePath: string, paused: bool] -> {outcome, id, message}
    AddMagnet               = 802,
    /// [id: string, deleteFiles: bool] -> bool
    RemoveTorrent           = 803,
    /// [id: string] -> bool
    PauseTorrent            = 804,
    /// [id: string] -> bool
    ResumeTorrent           = 805,
    /// [id: string] -> {files: [{index, path, size, done, priority, pad}],
    ///                  peers: [{address, client, progress, rateDown, rateUp,
    ///                           downloaded, uploaded, seed, encrypted, utp, source,
    ///                           flags}],
    ///                  trackers: [{url, tier, working, message, seeds, peers,
    ///                              downloaded, nextAnnounce, updating, fails,
    ///                              hasError}],
    ///                  availabilityMap: bytes, options: {see SetTorrentOptions}}
    /// flags: Transmission's peer letters; nextAnnounce: seconds, -1 = none
    GetTorrentDetails       = 806,
    /// [id: string, priorities: [int 0..7, one per file]] -> bool
    SetTorrentFilePriorities = 807,
    /// [id: string] -> bool
    RecheckTorrent          = 808,
    /// [id: string] -> bool
    ReannounceTorrent       = 809,
    /// [id: string, {downloadLimitKb, uploadLimitKb, maxPeers, ratioMode,
    ///  ratioPercent, seedTimeMode, seedTimeMinutes}] -> bool
    /// Limits in KiB/s, 0 = none; maxPeers 0 = default; modes are BtLimitMode
    /// (0 global, 1 unlimited, 2 custom)
    SetTorrentOptions       = 810,
    /// [id: string, urls: [string]] -> bool
    AddTorrentTrackers      = 811,
    /// [id: string, urls: [string]] -> bool
    RemoveTorrentTrackers   = 812,
    /// [] -> {running, listenPort, listening, dhtNodes, torrents, rateDown, rateUp}
    GetBtStatus             = 818,
    /// [] -> [ok, map]. The BitTorrent branch of the Statistics window; answered
    /// with the engine off too (the totals are core's).
    /// { session: BtCounters, cumulative: BtCounters,
    ///   current: { running, downRate, upRate (B/s payload), downLimitKb, upLimitKb
    ///              (0 = none), peers, dhtNodes,
    ///              torrents: { count, downloading, seeding, paused, checking, queued,
    ///                          error, totalBytes, doneBytes, leftBytes } } }
    GetBtStats              = 819,

    // -- ed3k (1000-1049; more in 1100-1149) -----------------------------------------------------
    //
    // The ed3k node (packmule, src/ed3k). Maps of src/daemon/Ed3kBridge.cpp. Answered
    // with the node off or not built too (available / running false).

    /// [] -> [ok, {available, running, udpPort, nodeId (hex), contacts, class4, class6
    ///   (0 unknown, 1 open, 2 cone, 3 symmetric), router, wireSent, wireRecv (bytes),
    ///   rateUp, rateDown (B/s), limitUpKb, limitDownKb (0 = none),
    ///   stats: {connections, lookups, records, recordBytes, published, queueLen,
    ///           bannedNodes, bannedHosts, sentFile, sentOther, recvFile, recvOther},
    ///   session, cumulative: Ed3kCounters (stats/NetworkCounters.h; session = this daemon run),
    ///   queue: {slotCount, holders, waiting} (engine's upload queue policy, while running),
    ///   credit: {enabled, configured, endpoints, address, problem, balance (µ, -1 unknown),
    ///           mature, overdrawn, lastTxWhat, lastTxState, lastTxRevert, lastTx,
    ///           receiptsSigned, kibSigned, receiptsTaken, kibTaken, slotHints, lateBids,
    ///           wrongChain: [endpoint], stakeRate (uint256 hex, "" unknown), stakeDelay (s),
    ///           claims: [{id, to, amount (uint256 hex), readyAt, state}]}}]
    GetEd3kStatus           = 1000,

    // ed3k friends and chat (1001-1009, src/daemon/Ed3kFriendsBridge.cpp). Keys (32 B) and
    // nodes (16 B) as hex; a chat is keyed by node. Failure: [false, {error: ed3k::Error}].
    /// [] -> [ok, {running, friends: [{key, node, name, mutual, asking, heldLink, addr,
    ///   lastSeen}], requests: [{key, node, text, issued}]}]
    GetEd3kFriends          = 1001,
    /// [link: string, text: string] -> [ok, {key}]; text goes with a request (`r`), <= 96 B
    AddEd3kFriend           = 1002,
    /// [key] -> [ok]
    RemoveEd3kFriend        = 1003,
    /// [key, accept: bool] -> [ok]
    AnswerEd3kFriendRequest = 1004,
    /// [withRequest: bool, name: string] -> [ok, link: string]
    GetEd3kMyFriendLink     = 1005,
    /// [node, text] -> [ok, ids: array]; each id ends as PushEd3kChatSent / _Failed
    SendEd3kChat            = 1006,
    /// [node] -> [ok] — the chat window closed
    EndEd3kChat             = 1007,

    // ed3k readable names and websites (1010-1019, src/daemon/Ed3kNamesBridge.cpp, name.md).
    // Zones/keys as hex. Failure: [false, {error: ed3k::Error}] (0: no node).
    /// [text: string (ed3k:name link, petname, alice, a domain, a handle)] -> [ok, {id}];
    /// PushEd3kNameResolved / PushEd3kNameFailed follow with that id
    ResolveEd3kName         = 1010,
    /// [id] -> [ok, {links: [string], url: string}] — the user opens it (pinned, §4.4);
    /// url: the website of the first ed3k:set link (empty: none, open the links)
    OpenEd3kName            = 1011,
    /// [] -> [ok, {running, sitePort, pins: [{name, zone, since, how, howA, howB, vouched,
    ///   link}], petnames: [{text, zone, shareable}], lists: [{id, link, label, entries,
    ///   refused}]}] (lists in the user's order: the first wins)
    GetEd3kNames            = 1012,
    /// [text, zone (hex or base32), shareable: bool] -> [ok]
    SetEd3kPetname          = 1013,
    /// [text] -> [ok]
    RemoveEd3kPetname       = 1014,
    /// [name] -> [ok] — only the user changes a pin
    UnpinEd3kName           = 1015,
    /// [name, on: bool] -> [ok] — vouching publishes a claim (§4.2)
    VouchEd3kName           = 1016,
    /// [link (ed3k:set), label] -> [ok, {id}]
    FollowEd3kNameList      = 1017,
    /// [id] -> [ok]
    UnfollowEd3kNameList    = 1018,
    /// [id, place] -> [ok] — 0 = first
    MoveEd3kNameList        = 1019,

    // ed3k credits (1020-1029, src/daemon/Ed3kCreditBridge.cpp, credit.md). The credit key is
    // a secret: these go to a local GUI only (403 for a remote one); never logged.
    // Failure: [false, {error: string}]. The rest is in GetEd3kStatus "credit".
    /// [] -> [ok, {address}] — a new key replaces credit.key (a new address, §2)
    NewEd3kCreditKey        = 1020,
    /// [key: hex (64)] -> [ok, {address}] — replaces credit.key
    ImportEd3kCreditKey     = 1021,
    /// [] -> [ok, {key: hex (64), address}]
    ExportEd3kCreditKey     = 1022,

    // -- ed3k stake (batch 12): the stake token of credit.md §6.7 (Ed3kCreditBridge). The main
    // credit key only, never the anonymous one (§9.1). Failure: [false, {error}] (ed3k::Error
    // as int, 0: no node / credits not in use).
    /// [micro: int (µ credits), to: string (0x + 40 hex)] -> [ok] — a burn cannot be undone
    /// and is public (§6.7, §9); the claim follows as PushEd3kStakeClaims
    BurnEd3kStake           = 1023,
    /// [id: int] -> [ok] — claimStake of a kept claim (ready); PushEd3kStakeTx / _Claims follow
    ClaimEd3kStake          = 1024,
    /// [] -> [ok, {claims: [{id, to, amount (uint256 hex), readyAt, state (0 waiting,
    ///   1 claiming, 2 paid)}], rate (uint256 hex, "" unknown), delay (s), address}]
    GetEd3kStakeClaims      = 1025,
    /// [] -> [ok] — reads every claim not paid again (stakeClaims); PushEd3kStakeClaims follows
    RefreshEd3kStakeClaims  = 1026,
    // -- ed3k stake typed ids (batch 13), Ed3kCreditBridge. Claims also carry burnTx, claimTx
    // (hash hex, "" unknown). ClaimEd3kStake takes a kept id or one read by this (§9.1)
    /// [id: int] -> [ok] — reads stakeClaims(id) of any id; PushEd3kStakeLookup follows
    ReadEd3kStakeClaim      = 1027,

    // ed3k plumbing (1030-1039): node list sources (bootstrap.md §3), Ed3kBridge.cpp
    /// [link: string (ed3k:nodes?url=…&key=…)] -> [ok, {ask}]; PushEd3kBootstrapAsk follows.
    /// Failure: node off, link invalid
    OpenEd3kNodesLink       = 1030,
    /// [ask: int, yes: bool] -> [ok]; yes adds the source to ed3kNodeLists
    AnswerEd3kBootstrapAsk  = 1031,

    // hoster links (1100-1109, hoster.md), Ed3kHosterBridge.cpp. An item: {id, url, host,
    // name, size, done, speed, state (Ed3kHosterJoin::State), reason (Ed3kHosterJoin::Reason, the
    // GUI words it), reasonArg (technical, not localized), waitUntil (ms), verified, file, root}
    /// [] -> [ok, {items: [item], status: {...GetEd3kStatus "hoster"...}}]
    GetHosterItems          = 1100,
    /// [text: string (links, one per line), files?: [[url, mirror…]]] -> [ok, {ids: [int]}];
    /// files given: one item per file, its other links tried when the first fails or is
    /// stuck, text ignored. PushHosterItem follows each
    AddHosterLinks          = 1101,
    /// [id: int, op: "pause"|"resume"|"remove"|"delete"] -> [ok]; delete also removes a complete file
    HosterAction            = 1102,
    /// [puzzle: int, text: string] -> [ok]; empty text skips the puzzle
    AnswerHosterPuzzle      = 1103,
    /// [host, user, password] -> [ok]; PushHosterAccount follows. Local clients only (a password)
    CheckHosterAccount      = 1104,
    /// [] -> [ok]; fetches a Java runtime into the eMuleQt folder (PushEd3kStatus shows progress)
    DownloadJavaRuntime     = 1105,
    /// [] -> [ok]; looks for a Java runtime again
    LocateJavaRuntime       = 1106,
    /// [] -> [ok, map]. The Hoster branch of the Statistics window; failure without a join.
    /// { session: HosterCounters, cumulative: HosterCounters,
    ///   current: { downRate (B/s), items: { count, downloading, queued, waiting, puzzle,
    ///              paused, needsUser, complete, failed, totalBytes, doneBytes, leftBytes } } }
    GetHosterStats          = 1107,
    /// [urls: array] -> [ok]; checks each link through the core as its download would (a links
    /// dialog of a web search row). PushHosterLinkCheck follows each, a fresh result at once.
    /// Failure while hoster downloads, the resolver or ed3kHosterCheckSearchLinks are off
    CheckHosterLinks        = 1108,

    // -- ed3k downloads (batch 12): plain ed3k files (search rows, ed3k:file links without bt=),
    // src/daemon/Ed3kDownloadsBridge.cpp. Roots (32 B) as hex. A download: {root, name, size,
    // done, rate (B/s), sources, transferring, state (0 downloading, 1 paused, 2 verifying,
    // 3 complete, 4 failed), priority (eMule's kPr*: 0 low, 1 normal, 2 high, 3 very high,
    // 4 very low), category, owner, path, addedOn, completedOn, error}; a local GUI opens
    // `path` itself. Failure: [false,
    // {error: string (localized)}]. (1100-1109 were taken by the hoster links: 1140-1149.)
    /// [] -> [ok, {running, downloads: [download]}]
    GetEd3kDownloads        = 1140,
    /// [root, size, name, category, paused] -> [ok, {root}]
    AddEd3kDownload         = 1141,
    /// [link (ed3k:file without bt=), category, paused] -> [ok, {root}]
    AddEd3kDownloadLink     = 1142,
    /// [root, action (0 pause, 1 resume, 2 cancel, 3 priority, 4 category, 5 clear completed:
    /// root ignored), value] -> [ok]
    Ed3kDownloadControl     = 1143,

    // -- ed3k filter lists (batch 12): filter-list.md, src/daemon/Ed3kFiltersBridge.cpp.
    // Entry ids (16 B) as hex; subject 1 file root (first 16 B), 2 ed2k MD4, 3 author (node
    // ID); verdict 0 none, 1 fake, 2 harmful, 3 spam, 4 good. Failure:
    // [false, {error: ed3k::Error}] (0: no node).
    /// [] -> [ok, {running, lists: [{id, label, mode (bit 1 hide, 2 refuse), byHand, set (hex,
    ///   "" none), seq (-1 none yet), files, entries, waiting}], ownMode, own: [{subject, id,
    ///   verdict, reason}], batch 13: published: {link ("" none), title}}]
    GetEd3kFilters          = 1130,
    /// [link (ed3k:set), label] -> [ok, {id}]
    FollowEd3kFilter        = 1131,
    /// [id] -> [ok]
    UnfollowEd3kFilter      = 1132,
    /// [id (0: the own list), hide: bool, refuse: bool] -> [ok]
    SetEd3kFilterMode       = 1133,
    /// [subject, id (hex), verdict (0 takes it back), reason] -> [ok]
    SetEd3kFilterOwn        = 1134,
    /// [subject, id (hex)] -> [ok, {hide, refuse, shown, own (verdict, -1 none), ownReason,
    ///   said: [{filter, verdict, label, title, reason}]}]
    JudgeEd3kFilter         = 1135,
    /// [label, file: bytes (<= 4 MiB)] -> [ok, {id}] — a list file opened by hand (§2: no
    /// author, no updates)
    OpenEd3kFilterFile      = 1136,

    // -- ed3k rooms (batch 12): room.md, src/daemon/Ed3kRoomsBridge.cpp. Rooms (16 B) and post
    // ids (16 B) as hex, member keys as base32 (52, §1.1). A link with `s` is a credential:
    // never logged. Failure: [false, {error: ed3k::Error}] (0: no node).
    /// [] -> [ok, {running, rooms: [{room, link, title, about, nick, owner, moderator, private,
    ///   closed, moderated, joined, neighbours, generation, keyHeld, keyMissing, memberKey,
    ///   mods: [key], members: [{key, nick, moderator}], batch 13: muted: [member ID hex],
    ///   notAdmitted (a member answered ROOM_MEMBER since the last join)}]}]
    GetEd3kRooms            = 1120,
    /// [room] -> [ok, posts: [{id, author, authorId, nick, moderator, sent, text, replyTo}]]
    GetEd3kRoomPosts        = 1121,
    /// [title, about, flags (ED3K_ROOM_PRIVATE 1, _CLOSED 2, _MODERATED 4), nick] -> [ok, {room, link}]
    CreateEd3kRoom          = 1122,
    /// [link (ed3k:room?...), nick] -> [ok, {room}]
    JoinEd3kRoom            = 1123,
    /// [room] -> [ok]
    LeaveEd3kRoom           = 1124,
    /// [room, text, replyTo (hex, "" none)] -> [ok, ids: array]; shown as PushEd3kRoomPost
    PostEd3kRoom            = 1125,
    /// [room, nick] -> [ok]
    SetEd3kRoomNick         = 1126,
    /// [room, title, about, flags (_CLOSED 2, _MODERATED 4), mods: [key]] -> [ok] — owner
    SetEd3kRoomInfo         = 1127,
    /// [room, action (0 admit, 1 ban, 2 lift, 3 new generation), member key, until (Unix, 0)]
    ///   -> [ok, {admit: text} | {generation}] — moderator
    ModerateEd3kRoom        = 1128,
    /// [admit text (ed3k-admit:...)] -> [ok, {room}] — newcomer of a closed room
    ImportEd3kRoomAdmit     = 1129,

    // -- ed3k filters + rooms polish (batch 13): Ed3kFiltersBridge / Ed3kRoomsBridge. Failure as
    // their blocks above.
    /// [title, on: bool] -> [ok, {link}] — publish the own filter list as a set (filter-list
    /// §2), again on every change; on false stops (link "")
    PublishEd3kFilterOwn    = 1042,
    /// [root (hex, 64), on: bool] -> [ok] — "show anyway" (§3): a local choice for that file
    ShowEd3kFilterAnyway    = 1043,
    /// [room, member ID (hex, 32), on: bool] -> [ok] — mute a member locally (room.md §6);
    /// kept in rooms/joined.cbor, GetEd3kRooms rows list it in `muted`
    MuteEd3kRoomMember      = 1044,

    // -- ed3k sets and live (batch 12): followed sets (file-set.md §5.3) and live streams
    // (live.md), src/daemon/Ed3kSetsBridge.cpp. Set ids are numbers, roots and streams hex.
    // A set: {id, link ("" for a hidden one: a credential), title, author (key hex), hidden,
    // pinned, followed, policy (0 ask, 1 download), seq / pendingSeq (-1 none), added, changed,
    // removed (paths of the pending version), dir, error, totalSize, members: [{path, root,
    // size, skipped, state (0 skipped, 1 waiting, 2 downloading, 3 complete, 4 failed,
    // 5 paused), done}], parts: [{author, seq, dir, name, files, size}]}. A stream: {id,
    // title, media, codecs, state (0 joining, 1 playing, 2 interrupted, 3 no peers, 4 ended),
    // neighbours, segments, bytes, viewers, url}. Failure: [false, {error: string (localized)}].
    /// [] -> [ok, {running, sets: [set]}]
    GetEd3kSets             = 1110,
    /// [link (ed3k:set), policy] -> [ok, {id}]
    FollowEd3kSet           = 1111,
    /// [id] -> [ok] — the member files stay
    UnfollowEd3kSet         = 1112,
    /// [id, policy] -> [ok]
    SetEd3kSetPolicy        = 1113,
    /// [id, skipped: [path]] -> [ok] — members (or directories) left out
    SetEd3kSetSelection     = 1114,
    /// [id] -> [ok] — take the pending version, download what the selection wants
    ApplyEd3kSetVersion     = 1115,
    /// [id, author (key hex, "" every author)] -> [ok] — open parts (multi-author-set.md §3.5)
    DownloadEd3kSetParts    = 1116,
    /// [link (ed3k:live)] -> [ok, {id, url}]
    WatchEd3kLive           = 1117,
    /// [id] -> [ok]
    LeaveEd3kLive           = 1118,
    /// [] -> [ok, {running, streams: [stream]}]
    GetEd3kLive             = 1119,
    /// [link (ed3k:set)] -> [ok, {url}] — the set's website origin (name.md §6.2; "" while
    /// the website server does not listen)
    GetEd3kSetSite          = 1144,

    // -- ed3k paid subscriptions and live broadcasting (batch 13): subscription.md (buying
    // passes), live.md §4. src/daemon/Ed3kPaidBridge.cpp (paid), Ed3kSetsBridge.cpp
    // (broadcast; LeaveEd3kLive ends a broadcast, GetEd3kLive lists them with broadcast: true,
    // link, folder, error). A scope: 32 hex digits. A subscription: {scope, kind (1 channel,
    // 2 set, 3 hidden set), title, anon, state (0 no pass, 1 buying, 2 held, 3 refused,
    // 4 gave up, 5 anonymous warning waits), until (Unix s, 0 none), renewing, renewalEnded,
    // keys, newestEpoch, price, terms, payHost, period (s), devices, hasOffer}.
    /// [] -> [ok, {running, reports, subs: [subscription]}]
    GetEd3kPaid             = 1035,
    /// [scope, anon: bool] -> [ok, {payLink}] — open payLink in the browser; "" = the user must
    /// be told first (PushEd3kPaidAnonWarn, then ConfirmEd3kPaidAnon)
    BuyEd3kPass             = 1036,
    /// [scope] -> [ok, {payLink}] — the user was told the seller sees an address (§5.1);
    /// payLink of a purchase ("" for a renewal or resumed poll)
    ConfirmEd3kPaidAnon     = 1037,
    /// [on: bool] -> [ok] — share reports (§5.3)
    SetEd3kPassReports      = 1038,
    /// [folder, title, private: bool, playlist ("" = the newest .m3u8), codecs] -> [ok, {id, link}]
    BroadcastEd3kLive       = 1039,

    // -- Responses (Core -> GUI) ---------------------------------------------

    HandshakeOk          = 300,  ///< [version, motd, sessionToken, loading: bool, loadStage: string]
    Result               = 301,  ///< [success, data]
    Error                = 302,  ///< [code, message]

    // -- Push Events (Core -> GUI, seqId=0) ----------------------------------

    /// {sessionSentBytes, sessionReceivedBytes, upWaiting, maxDownloadKb,
    /// usenetLimitKb, ed2kBudgetKb} — same keys and meaning as GetStats.
    PushStatsUpdate      = 400,
    PushDownloadUpdate   = 410,
    PushDownloadAdded    = 411,
    PushDownloadRemoved  = 412,
    /// [kind: NotifierEvent, text] — something the Notifications page offers a pop-up
    /// for. Never coalesced: each one is an event, not a state.
    PushNotifierEvent    = 413,
    PushServerState      = 420,
    PushServerMessage    = 421,  ///< [id, type: ServerMsgType, text: string] — one Server Info line
    PushSearchResult     = 430,
    PushGlobalSearchProgress = 431,  ///< [searchID, asked, total, running] — ED2K global UDP sweep
    /// [searchID, state: SearchRunState (0 queued, 1 running, 2 finished, 3 failed),
    ///  reason: string (queued: "waiting-for-server-connection" / "waiting-for-kad" /
    ///  "waiting-for-connection" / "waiting-for-previous-search"), error: string (failed),
    ///  type: SearchType (the network used once sent), keyword, primaryKeyword,
    ///  hasMore: bool — finished with a further page, see SearchMore].
    /// The StartSearch reply carries the same `state` and `reason`.
    PushSearchState      = 432,
    /// [loading: bool, stage: string] — the core started or finished reading its
    /// files. While loading, list requests are held and answered once it is over.
    PushCoreLoading      = 440,
    /// [] — the core is about to restart itself (REST API or another GUI asked).
    /// The connection drops; wait for it to come back instead of launching one.
    PushCoreRestarting   = 441,
    PushLogMessage       = 450,  ///< [logId, category, severity: QtMsgType, message, timestamp: unix s]
    /// [entries: array of [logId, category, severity, message, timestamp]] — every
    /// line of one relay flush in a single frame, same entry shape as SyncLogs.
    PushLogBatch         = 451,
    /// [rows: array] — rows of shared files that were added or changed, same shape as
    /// a GetSharedFiles row. Batched; a file is sent at most a few times a second.
    PushSharedFileUpdate = 460,
    PushSharedFileRemoved = 461,  ///< [hash] — no longer shared
    PushUploadUpdate     = 470,
    PushKadUpdate        = 480,
    PushKadSearchesChanged = 481,
    PushKnownClientsChanged = 490,
    PushChatMessage       = 500,  ///< [senderHash, senderName, message]
    PushChatState         = 501,  ///< [friendHash, ChatConnectProgress] — dial progress
    PushChatCaptcha       = 502,  ///< [senderHash, senderName, pngBase64] — solve it to chat
    PushChatCaptchaResult = 503,  ///< [senderHash, solved: bool] — the peer's verdict
    PushFriendListChanged = 510,  ///< [] — friend list changed
    /// [hash, userName, frames: bytes[] (PNG)] — the answer to RequestSearchPreview;
    /// no frames means the peer had nothing to show or could not be reached.
    PushSearchPreview     = 521,
    PushClientSharedFiles = 520,  ///< [clientHash, userName, searchID] — browse answer landed in that Search tab
    PushPortMapStatus     = 530,  ///< [{status, statusText, method, methodText, externalAddress}]
    /// [] — the category list changed; re-fetch with GetCategories.
    /// Carries no payload on purpose: every consumer wants the whole list
    /// anyway, and a second GUI editing categories must not race a diff.
    PushCategoriesChanged = 540,

    // -- Indexer pushes (900-909) --------------------------------------------

    /// [searchId: int, [{...one result row...}]]
    /// Rows as each indexer answers, so a fast one is not held up by a slow one.
    PushIndexerResults     = 900,
    /// [searchId: int, done: int, total: int] — indexers answered, of how many.
    PushIndexerProgress    = 901,
    /// [searchId: int, error: string]
    /// Terminal, and **uncoalesced**: it is a transition, not a latest value.
    /// `error` summarises the indexers that failed and is often set alongside a
    /// perfectly good set of rows — one indexer being down is not a failed
    /// search.
    PushIndexerSearchDone  = 902,
    /// [{name, lastPolled, lastError, lastMatched, seenCount, polling}]
    /// Coalesced on the feed name. A feed's whole point is that it acts while
    /// nobody is watching, so "last checked, this many matched, this error" is
    /// the only visibility there is.
    PushIndexerFeedStatus  = 903,

    // -- Usenet pushes (910-949) ---------------------------------------------
    //
    // Same reasoning as the 720 request block: a second network must not eat the
    // handful of push slots the ED2K core has left. 900-909 is reserved for the
    // shared indexer client.

    /// [{...one item, same shape as a GetUsenetQueue row...}]
    /// Per-item and coalesced on the item id, so a 10 000-article release cannot
    /// suppress pushes for a second NZB queued beside it.
    PushUsenetQueueItem   = 910,
    /// [id: string] — the item is gone; drop the row.
    PushUsenetItemRemoved = 911,
    /// [id: string, success: bool, message: string]
    /// Terminal outcome, broadcast **uncoalesced**: this is a transition, not a
    /// latest value, and a coalescing window would swallow it whole.
    PushUsenetItemFinished = 912,
    /// [{paused: bool}] — the engine-wide pause changed. Uncoalesced: a
    /// transition, and the tray and the toolbar both follow it.
    PushUsenetEngineState  = 913,

    // -- BitTorrent pushes (950-999) -------------------------------------------

    /// [{...one row, same shape as a GetTorrents row...}] — a new torrent.
    /// Uncoalesced: a transition.
    PushTorrentAdded       = 950,
    /// [{...one row...}] — coalesced on the id.
    PushTorrentUpdate      = 951,
    /// [id: string] — the torrent is gone; drop the row.
    PushTorrentRemoved     = 952,
    /// [{...GetBtStatus...}] — the engine started, stopped or changed its port.
    PushBtStatus           = 953,
    /// [id: string] — every wanted byte is there. Uncoalesced.
    PushTorrentFinished    = 954,

    // -- ed3k pushes (1050-1099; more in 1150-1199) -----------------------------------------------

    /// [{...GetEd3kStatus...}] — the node started or stopped, its contacts, address
    /// class or router duty changed. A state push: a slow reader keeps the last.
    PushEd3kStatus         = 1050,
    /// [] — the ed3k friend list, a name, an address or a request changed
    PushEd3kFriendsChanged = 1051,
    /// [node, name, text, id] — a chat message to show once
    PushEd3kChatMessage    = 1052,
    /// [node, id] — the peer's node has the message (ChatAck)
    PushEd3kChatSent       = 1053,
    /// [node, id, why: ED3K_ASK_*, retryAfter: s]
    PushEd3kChatFailed     = 1054,
    /// [key, node, text, issued] — accept or decline (AnswerEd3kFriendRequest)
    PushEd3kFriendRequest  = 1055,
    /// [node, ChatConnectProgress] — a send waits for a look of the friend
    PushEd3kChatState      = 1056,
    /// [{id, name, zone, label, links, how, howA, howB, howTitle, warnings: [{zone, how, howA,
    ///   howB}], ties: [zone], confirm, webDisagree, viaSite}] — show how it was resolved,
    /// ask when confirm or how is DNS-unverified, then OpenEd3kName. viaSite: typed in the
    /// browser (a readable host of the website server), sent only when it needs the yes
    PushEd3kNameResolved   = 1060,
    /// [{id, name, why: ED3K_NAME_WHY_*, ties: [zone]}]
    PushEd3kNameFailed     = 1061,
    /// [] — pins, petnames or followed name lists changed: GetEd3kNames
    PushEd3kNamesChanged   = 1062,
    /// [node, link] — a friend publishes a name list (§4.5): offer FollowEd3kNameList
    PushEd3kNameFriendList = 1063,
    /// [ask: int, host: string, url: string] — an ed3k:nodes link wants to add a node list
    /// source: ask the user (show host), answer with AnswerEd3kBootstrapAsk. Sent only
    /// while a GUI is connected; else the daemon refuses at once
    // -- ed3k stake (batch 12), see BurnEd3kStake
    /// [{...GetEd3kStakeClaims...}] — the claims of own burns changed (a state push)
    PushEd3kStakeClaims    = 1076,
    /// [{what (ED3K_CREDIT_TX_BURN_FOR_STAKE 3 / _CLAIM_STAKE 4), state (ED3K_CREDIT_STATE_*),
    ///   revert (ED3K_CREDIT_REVERT_*), tx (hash hex)}] — a stake transaction moved
    PushEd3kStakeTx        = 1077,
    // -- ed3k stake typed ids (batch 13), see ReadEd3kStakeClaim
    /// [{id, found (false: never made or paid), paid (not found: it was made), failed (the read
    ///   failed), to, amount (uint256 hex), readyAt, kept}] — show it; ClaimEd3kStake when found and ready
    PushEd3kStakeLookup    = 1078,
    PushEd3kBootstrapAsk   = 1080,

    // hoster links (1150-1159)
    /// [item] — added or changed; coalesced on the id
    PushHosterItem         = 1150,
    /// [id: int]
    PushHosterItemRemoved  = 1151,
    /// [item: int (0: none), puzzle: int, image: bytes, host, explain, timeout: s] — show the
    /// image puzzle, answer with AnswerHosterPuzzle
    PushHosterPuzzle       = 1152,
    /// [item: int, links: [{url, name, host, size}]] — a folder link: ask which to add (AddHosterLinks)
    PushHosterLinks        = 1153,
    /// [host, user, {valid, premium, expires, trafficLeft, error}]
    PushHosterAccount      = 1154,
    /// [url, {status (Ed3kHosterJoin::LinkStatus: 1 online, 2 offline, 3 unknown), host, name,
    ///   size, account (0 none, 1 an account, 2 premium with traffic left)}] — a CheckHosterLinks result
    PushHosterLinkCheck    = 1159,

    // -- ed3k downloads (batch 12), see GetEd3kDownloads (1150-1154 were taken by the hoster links)
    /// [{...GetEd3kDownloads...}] — the ed3k download list changed (a state push, at most once a second)
    PushEd3kDownloads      = 1180,

    // -- ed3k filter lists (batch 12), see GetEd3kFilters
    /// [] — followed lists, their versions or the own verdicts changed (a state push)
    PushEd3kFiltersChanged = 1170,

    // -- ed3k rooms (batch 12), see GetEd3kRooms
    /// [] — the rooms, an info, a nick or a room's state changed (a state push): GetEd3kRooms
    PushEd3kRoomsChanged   = 1160,
    /// [room, {id, author, authorId, nick, moderator, sent, text, replyTo}] — a post to show once
    PushEd3kRoomPost       = 1161,
    /// [room, ids: array] — posts dropped by a ban
    PushEd3kRoomPostsRemoved = 1162,
    /// [room, member key, nick, left: bool, member id: hex]
    PushEd3kRoomPresence   = 1163,
    /// [room, kind (Ed3kRooms::Notice), value, flag: bool, who (node hex, member key base32, "")]
    PushEd3kRoomNotice     = 1164,

    // -- ed3k sets and live (batch 12), see GetEd3kSets / GetEd3kLive
    /// [{...GetEd3kSets...}] — followed sets, versions or member states changed (a state push)
    PushEd3kSetsChanged    = 1155,
    /// [id, title, seq, added, changed, removed] — a new version of a set with policy ask
    PushEd3kSetVersion     = 1156,
    /// [{...GetEd3kLive...}] — a stream's state changed (a state push)
    PushEd3kLiveChanged    = 1157,
    /// [link] — the user opened a set that is no website (name.md §6.1): offer it for download
    PushEd3kSetOffer       = 1158,

    // -- ed3k paid subscriptions (batch 13), see GetEd3kPaid
    /// [{...GetEd3kPaid...}] — a subscription, offer or pass changed (a state push)
    PushEd3kPaidChanged    = 1064,
    /// [scope, title] — §5.1: tell the user the seller sees an address, then ConfirmEd3kPaidAnon
    PushEd3kPaidAnonWarn   = 1065,
    /// [scope, title, until] — a pass arrived (bought or renewed)
    PushEd3kPassArrived    = 1066,
};

/// What SetCategoryStatus should do to every download in the category.
///
/// MFC drives the same five from its category tab's context menu with the
/// generic MP_PAUSE/MP_STOP/MP_CANCEL/MP_RESUME/MP_RESUMENEXT command ids
/// (srchybrid/TransferWnd.cpp:951-966). Named here because those are Windows
/// menu constants, and an IPC contract should not depend on a resource header.
enum class CategoryAction : int {
    Pause = 0,
    Resume = 1,
    Stop = 2,
    /// Deletes the part files. The GUI confirms first — MFC asks
    /// IDS_Q_CANCELDL — but the daemon does not second-guess the answer.
    Cancel = 3,
    /// Resume the single highest-ranked paused download in this category.
    ResumeNext = 4,
};

/// Outcome of an eNode Meta API request (the `status` key of a meta map).
enum class MetaStatus : int {
    Ok = 0,
    NoMetaApi,         ///< the row's server announced no Meta API
    AuthRequired,      ///< log in first (show the login dialog)
    AccountInactive,   ///< logged in, but registration steps are open
    NotFound,
    VerifyFailed,      ///< the bytes do not match the row's meta hash
    Unavailable,
    RateLimited,
    TooLarge,
    Error,
};

/// MetaStatus pending-step kinds mirror enode.meta.v1.StepKind; account states
/// mirror AccountState (1 pending, 2 active, 3 expired, 4 disabled).

// ---------------------------------------------------------------------------
// Framing constants
// ---------------------------------------------------------------------------

/// Size of the length prefix in bytes.
inline constexpr int FrameHeaderSize = 4;

/// Maximum allowed payload size (16 MiB).
inline constexpr uint32_t MaxPayloadSize = 16 * 1024 * 1024;

/// Maximum number of hashes one GetEd2kLink request may carry. A hashset link for a
/// multi-GB file runs past 10 KB, and an oversized frame does not merely fail — it drops
/// the IPC connection (IpcConnection::onReadyRead) — so both ends clamp instead of
/// trusting how much the user selected.
inline constexpr int MaxEd2kLinkBatch = 1000;

/// AddServer Error code: the address is LAN/loopback and filterLANIPs is on. Distinct
/// from a duplicate (Result false), so the GUI can tell the user instead of staying quiet.
inline constexpr int ErrServerLanFiltered = 422;

/// Unsent bytes towards one client above which state pushes are held back (1 MiB).
inline constexpr qint64 PushHoldBacklog = 1024 * 1024;

/// Unsent bytes above which a client counts as stuck and is dropped (64 MiB). Well
/// past MaxPayloadSize, so one large reply on an idle connection never trips it.
inline constexpr qint64 MaxWriteBacklog = 64LL * 1024 * 1024;

/// Sent with Error when a held request was dropped for a newer identical one, or
/// the core is too busy to hold more.
inline constexpr int ErrSuperseded = 503;

/// Default IPC TCP port.
inline constexpr uint16_t DefaultIpcPort = 4712;

/// Protocol version string.
inline constexpr const char* ProtocolVersion = "1.1";   // 1.1: TLS replaces AES-CBC

// ---------------------------------------------------------------------------
// Framing functions
// ---------------------------------------------------------------------------

/// Encode a CBOR payload into a length-prefixed frame.
/// Returns [4-byte big-endian length][payload].
[[nodiscard]] QByteArray encodeFrame(const QByteArray& cborPayload);

/// Convenience: encode a QCborArray directly into a frame.
[[nodiscard]] QByteArray encodeFrame(const QCborArray& message);

/// Result of a frame decode attempt.
struct DecodeResult {
    QCborArray message;    ///< Decoded CBOR array.
    int bytesConsumed = 0; ///< Total bytes consumed (header + payload).
};

/// Try to decode one frame from the front of @p buffer.
/// Returns std::nullopt if insufficient data or invalid framing.
/// On success, the caller should remove bytesConsumed from the buffer.
[[nodiscard]] std::optional<DecodeResult> tryDecodeFrame(const QByteArray& buffer);

} // namespace eMule::Ipc

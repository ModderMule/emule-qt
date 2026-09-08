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
    ClearCompleted       = 116,  ///< [] — remove completed downloads
    GetDownloadSources   = 117,  ///< [hash: string] — source clients for one download
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
    AddServer            = 135,  ///< [address: string, port: int64, name: string]
    SetServerOrder       = 136,  ///< [CborArray of [ip:int64, port:int64, addr:string]] (#24)
    GetConnection        = 140,
    ConnectToServer      = 141,  ///< [] or [ip: int64, port: int64, addr: string]
    DisconnectFromServer = 142,
    StartSearch          = 150,  ///< [expression, fileType, method, minSize, maxSize, avail, ext, completeSrc]
    GetSearchResults     = 151,  ///< [searchID]
    StopSearch           = 152,  ///< [searchID: int]
    RemoveSearch         = 153,  ///< [searchID: int]
    ClearAllSearches     = 154,  ///< []
    DownloadSearchFile   = 155,  ///< [hash: string, fileName: string, fileSize: int64, link: string] — link wins if set
    GetKnownTypes        = 156,  ///< [hashes: QCborArray of strings] → [types: QCborArray of ints]
    GetSharedFiles       = 160,
    SetSharedFilePriority = 161, ///< [hash: string, priority: int, isAuto: bool]
    ReloadSharedFiles    = 162, ///< [] — rescan shared directories from disk
    GetEd2kLink          = 163, ///< [hashes: QCborArray of strings, hashset: bool,
                                ///<  sourceHint: bool, html: bool]
                                ///<   → [ok: bool, [links: QCborArray of strings,
                                ///<                 sourceHintAvailable: bool]]
                                ///<   links[i] pairs with hashes[i]; a hash that no longer
                                ///<   resolves yields an empty string, never an error.
    GetFriends           = 170,
    AddFriend            = 171,  ///< [hash, name, ip, port, addr: string] — addr wins if set
    RemoveFriend         = 172,  ///< [hash]
    SendChatMessage      = 173,  ///< [hash: string, message: string]
    SetFriendSlot        = 174,  ///< [hash: string, enabled: bool]
    GetStats             = 180,
    GetPreferences       = 190,
    SetPreferences       = 191,  ///< [key, value, ...]
    Subscribe            = 200,  ///< [eventMask: int]
    GetKadContacts       = 210,
    GetKadStatus         = 211,
    BootstrapKad         = 212,  ///< [ip: string, port: int]  (empty = from nodes.dat)
    DisconnectKad        = 213,
    SyncLogs             = 214,  ///< [lastLogId: int64]  — request buffered logs since ID
    Shutdown             = 215,  ///< [] — request graceful daemon shutdown
    GetKadSearches       = 216,
    GetKadLookupHistory  = 217,  ///< [searchId: int] — lookup history for a search
    GetNetworkInfo       = 218,  ///< [] — all network info for the Network Information dialog
    RecheckFirewall      = 219,  ///< [] — restart TCP + UDP firewall checks
    ReloadIPFilter       = 220,  ///< [] — reload IP filter from ipfilter.dat
    GetSchedules         = 221,  ///< [] — returns schedulerEnabled + full schedule list
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
    SaveCollection          = 257, ///< [name, fileHashes[], textFormat, sign] → create & share
    // 258 was SearchAuthorCollections; the GUI now drives that search through StartSearch.
    GetServerMessages       = 259, ///< [fromId] → CborArray of [id, type, text] — Server Info backlog
    /// [searchID: int, hash: string] → comments/tags for one search result.
    /// The searchID is required because a hash is only unique within a search tab.
    GetSearchResultDetails  = 260,

    /// [] → re-read the web server template from disk. For "same path, edited
    /// content"; a changed templatePath is a config change and already goes
    /// through SetPreferences → restartWebServer().
    ReloadWebTemplate       = 272,

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
    /// [searchId: int, resultId: string] -> [ok, itemIdOrError]
    /// The daemon fetches the .nzb itself and hands it to the Usenet queue. The
    /// download URL carries the API key, so it never travels to the GUI and the
    /// GUI never issues the request.
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
    /// `automatic` decides which existing items refuse a re-add: a release still
    /// downloading is refused either way, a *completed* one only for an automatic
    /// add. Absent reads as false, which is what every GUI caller is — the field
    /// exists for the watch folder and for feeds.
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
    /// Field 1 is the same `automatic` bit AddNzb takes.
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

    // -- Responses (Core -> GUI) ---------------------------------------------

    HandshakeOk          = 300,  ///< [version, motd]
    Result               = 301,  ///< [success, data]
    Error                = 302,  ///< [code, message]

    // -- Push Events (Core -> GUI, seqId=0) ----------------------------------

    PushStatsUpdate      = 400,
    PushDownloadUpdate   = 410,
    PushDownloadAdded    = 411,
    PushDownloadRemoved  = 412,
    PushServerState      = 420,
    PushServerMessage    = 421,  ///< [id, type: ServerMsgType, text: string] — one Server Info line
    PushSearchResult     = 430,
    PushGlobalSearchProgress = 431,  ///< [searchID, asked, total, running] — ED2K global UDP sweep
    PushLogMessage       = 450,
    PushSharedFileUpdate = 460,
    PushUploadUpdate     = 470,
    PushKadUpdate        = 480,
    PushKadSearchesChanged = 481,
    PushKnownClientsChanged = 490,
    PushChatMessage       = 500,  ///< [senderHash, senderName, message]
    PushFriendListChanged = 510,  ///< [] — friend list changed
    PushClientSharedFiles = 520,  ///< [clientHash, CborArray of files] — response to browse
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

/// Default IPC TCP port.
inline constexpr uint16_t DefaultIpcPort = 4712;

/// Protocol version string.
inline constexpr const char* ProtocolVersion = "1.0";

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

// ---------------------------------------------------------------------------
// Raw frame extraction (for encrypted connections)
// ---------------------------------------------------------------------------

/// Raw frame payload without CBOR parsing.
struct RawFrameResult {
    QByteArray payload;
    int bytesConsumed = 0;
};

/// Extract one raw frame from the front of @p buffer (no CBOR parsing).
[[nodiscard]] std::optional<RawFrameResult> tryExtractRawFrame(const QByteArray& buffer);

// ---------------------------------------------------------------------------
// AES-256-CBC encryption helpers
// ---------------------------------------------------------------------------

/// Derive a 32-byte AES-256 key from a token string using SHA-256.
[[nodiscard]] QByteArray deriveAesKey(const QString& token);

/// AES-256-CBC encrypt: returns [16-byte IV][ciphertext].
[[nodiscard]] QByteArray aesEncryptPayload(const QByteArray& plaintext, const QByteArray& key);

/// AES-256-CBC decrypt: input is [16-byte IV][ciphertext]. Returns plaintext or empty on failure.
[[nodiscard]] QByteArray aesDecryptPayload(const QByteArray& data, const QByteArray& key);

} // namespace eMule::Ipc

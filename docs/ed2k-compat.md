# eD2K compat mode (ed3k Protocol.md §14)

State 2026-10-31. Spec: `docs/ed3k-spec/Protocol.md` §14, optional tags `docs/ed3k-spec/docs/ed2k-tags.md`.

## Done

| Item | Where |
|---|---|
| Derived user hash (§14.2), check bytes, hint parse | packmule `identity.rs`; FFI `ed3k_user_hash`, `ed3k_user_hash_hint`, `ed3k_seed_node_id` |
| New config: user hash derived from `node.key` | `Preferences::setUserHashSource` (daemon `main.cpp`), `ed3k::derivedUserHash`; GUI defers (`setDeferUserHash`) so the core mints it |
| New config: one port for eD2K TCP, eD2K UDP, ed3k UDP (§14.1) | `Preferences::newIdentity`, `randomSharedPort`; unpicked `ed3k.udpPort` takes the TCP number |
| ed3k shares the eD2K UDP socket (demux §14.1) | `ClientUDPSocket::setForeignHandler`, `Ed3kSession::offerDatagram`, `DaemonApp::applyEd3kConfig` |
| Migration of existing configs: button, plan, confirm | Options → ed3k → "eD2K compatibility"; IPC `MigrateEd2kCompat` (293); hash change restarts the core |
| One port field when TCP = UDP (wizard, Connection page, ed3k page) | `FirstStartWizard`, `OptionsDialog::setPortsJoined`, `Ed3kOptionsPages::portJoined` |
| Random listen ports keep the joined ports joined | `CoreSession::randomizeListenPorts` |
| One port mapping when shared | `CoreSession::buildPortMapRequests` |
| Own shared files published under their eD2K hash (`ed2k` ALIAS) | `Ed3kShared` |
| Search rows carry the root/ed2k pair (display only) | `SearchFile::setEd3k` |
| **Hint dialing (§14.2):** a download source with a derived user hash is asked over ed3k first, at its address and TCP port number (§14.1); `WRONG_PEER` is the engine's (`Expect::ObfId`) | `UpDownClient::askForDownload` → `AppContext::ed3kHint` → `Ed3kEd2kJoin::offerHint`; `ConnectingState::Ed3kHint`, `Ed3kRoute` |
| **Fallback (MUST):** no answer in 30 s, stream refused / unreachable / not found → eD2K dial; the source is never dropped | `UpDownClient::setEd3kRoute`, `Ed3kEd2kJoin::releaseHint` |
| **One download from both networks (§14.4):** each running eD2K part file looks up its root (`ed2k` ALIAS md4 ‖ size), fetches the blocks it misses into the part file (BLAKE3 verified, no sender blamed), skips what eD2K asks right now | `Ed3kEd2kJoin` (`wantedBlocks`, `onFileBlock` → `PartFile::writeToBuffer`) |
| Availability as byte ranges: the part file's gap list is the one store; eD2K part map and ed3k piece bitmap are read from it | `Ed3kEd2kJoin::shareVerified`, `blockServable` |
| Partial eD2K download served over ed3k: pieces whole from ed3k, or eD2K bytes in MD4-verified parts once the pair is trusted | `Ed3kEd2kJoin::sharedRead` |
| **Pair trust (§14.4):** trusted once a part with ed3k bytes passes MD4; a failing part whose blocks pass under the root → `CLAIM_FALSE` → pair false, root dropped, next candidate; a `FileStatus` naming another ed2k hash → pair false | `AppContext::partVerdict` (`PartFile::verifyChangedParts`), `Ed3kEd2kJoin::onPartVerdict`, `onStreamStatus`; FFI wrapper `aliasEd2kPartBlocks` |
| **Prefer ed3k for a dual peer (§14.4):** a client answered over ed3k is not asked over eD2K (state "Via ed3k"); its eD2K upload-queue request is refused while it waits / holds a slot on ed3k | `Ed3kRoute::Preferred`; `AppContext::isEd3kUploadPeer` → `Ed3kUploads::hasNodeStarting` in `UploadQueue::checkWaitingListAdmission` |
| Option "Download eD2K files from ed3k peers too" (pref `ed3k.jointDownloads`, default on) | `Ed3kOptionsPages`, `Preferences::ed3kJointDownloads` |
| File Details → General of a download: "ed3k" box (BLAKE3 root + Trusted/Unverified, Searching / Not found, ed3k sources, bytes from ed3k); Shared Files: BLAKE3 root, alias/keywords published | `Ed3kEd2kJoin::infoOf`, `Ed3kBridge::appendDownloadInfo` / `appendSharedInfo` → `GetDownloadDetails` / `GetSharedFileDetails` keys `ed3k*`; `FileDetailDialog` |
| Docker: one port 4662 for TCP and UDP (new containers; `EMULEQT_UDP_PORT` still splits) | `docker/emuleqt/{Dockerfile,entrypoint.sh,compose.yml}`, `docs/docker.md` |

Existing configs keep their hash and ports until the user migrates. An MFC identity import
(`LegacyIdentityImport`) keeps the imported hash on purpose (credits).

Tests: `tests/tst_Ed3kEd2kJoin.cpp` (three loopback nodes: fed download + trust, false pair by
part, false pair by FileStatus, hinted source preferred, unanswered hint, client wait/fallback).

## Skipped

1. **Explicit tags (ed2k-tags.md):** `CT_ED3K` / `ST_ED3K` in hello, ext SX, login, server ident;
   server rendezvous as ed3k punch signal (§5). Proposal only; a dual client works without them.
2. **Remote UIs:** web UI / REST have no migrate action, no joint-download option, and still
   show two port fields.

## Approximate

- eD2K bytes are not checked block by block under BLAKE3 before the pair is trusted (the FFI has
  no such call); the check is per part, only when MD4 fails (`aliasSearchPartFailed`).
- An ed3k download of the same root through `Ed3kDownloads` (ed3k link) wins: the joint
  download leaves that root alone (`setRootTaken`).
- A client is matched to an ed3k node only by the hint prefix (12 bytes) of its user hash.

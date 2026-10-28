# eD2K compat mode (ed3k Protocol.md §14)

State 2026-10-28. Spec: `docs/ed3k-spec/Protocol.md` §14, optional tags `docs/ed3k-spec/docs/ed2k-tags.md`.

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

Existing configs keep their hash and ports until the user migrates. An MFC identity import
(`LegacyIdentityImport`) keeps the imported hash on purpose (credits).

## Missing

1. **Hint on eD2K sources (§14.2):** `userHashHint` has no caller. Kad source results, SX v2-4,
   ext SX `CT_EMULE_USERHASH`, `OP_FOUNDSOURCES_OBFU` (0x80), `OP_CALLBACKREQUESTED` are not
   checked; no ed3k dial (`ED3K_SOURCE_HINT`), no `WRONG_PEER` check on the hinted ID, no
   fallback-to-eD2K bookkeeping.
2. **One download from both networks (§14.4):** an eD2K `PartFile` never looks up the `ed2k`
   alias on ed3k; ed3k downloads are their own list and part files (`*.ed3k.part`). Availability
   as shared byte ranges (eD2K part map + ed3k pieces) not there.
3. **Pair trust (§14.4):** `ed2k` of `FileInfo` / `FileStatus` is ignored
   (`Ed3kJoinBase` reads only `have`/`usable`); no "verified under both hashes" step, no dropping
   a false pair.
4. **Prefer ed3k for a dual peer (§14.4):** peers are never matched across networks; eD2K and
   ed3k slots are counted apart.
5. **Explicit tags (optional, ed2k-tags.md):** `CT_ED3K` / `ST_ED3K` in hello, ext SX, login,
   server ident; server rendezvous as ed3k punch signal (§5). Proposal only.
6. **Remote UIs:** the web UI / REST have no migrate action and still show two port fields.
7. **Docker image:** keeps 4662/tcp + 4672/udp (ed3k locked off there); not one port.

# BiglyBT "Locate Files": how it works

Reference note on BiglyBT's folder scan, the feature asked for in #224. Source:
`biglybt/uis/src/com/biglybt/ui/swt/views/utils/ManagerUtils.java` (`locateFiles`,
`locateFilesSupport`, `buildFileMap`, `locateSavePaths`). Compare with eMule Qt's local copies
(ed3k-module.md, torrent join): ours match by ed3k root of files already hashed; BiglyBT
matches by size + BitTorrent piece hash of any file in folders the user picks.

## Entry

- Right-click a download (or files of one) → *Locate Files…* → `locateFilesSupport` dialog:
  root folders, mode, link type, tolerance %, include skipped files, test only.
- Also offered when adding a torrent (`TorrentOpenOptions` list) to find its files before
  the first start.
- The download must be stopped (`STATE_STOPPED`/`STATE_ERROR`), else skipped with a log line.
- Everything runs on a background thread; progress goes to a text log window
  (`TextViewerWindow`), which also has the cancel (`quit[0]`).

## Modes

| mode | what a match does |
|---|---|
| `LINK` (0) | point the torrent file at the found file (`setLink`); link type internal (BiglyBT's own link table, capped by `MAX_FILES_FOR_INCOMPLETE_AND_DND_LINKAGE`) or hard link (`Files.createLink`, falls back to internal) |
| `COPY` (1) | copy found file to the torrent's target path |
| `MOVE` (2) | rename found file to the target path |
| `PIECE` (3) | copy only the pieces whose hash matches into the target file, piece by piece; mismatches don't stop the file |
| `RELOCATE` (4) | `locateSavePaths`: find where the whole download's save folder moved to, by counting files by relative path under each candidate dir (also tries the "incomplete file" suffix) |

After any change: `dm.forceRecheck()` — the normal full recheck decides what is really there.
*Test only* runs the same and logs "Would link/copy …".

## Scan (`buildFileMap`)

Walks every root folder recursively, builds `Map<Long size, Set<File>>`. Only sizes count at
this stage; nothing is read. The sorted size array (`file_lengths`) is kept for tolerance.

## Matching, per torrent file (phase 1)

1. Skip pad files, unselected files (when files were picked), complete files that exist,
   skipped (DND) files unless *include skipped*.
2. Candidates = files of exactly the same size. Pre-scan: when several files of the torrent
   have candidates under the same parent root, that root is preferred and tried first
   (`candidate_root_pref`) so one release folder wins over scattered copies.
3. With tolerance > 0 the candidates also include files whose size is within ±tolerance %
   (binary search in `file_lengths`), for files that were padded/truncated/edited.
4. For each candidate, hash test:
   - only pieces fully inside the file: start at the first piece boundary inside the file
     (`offset % pieceSize`), stop at `fileLength - pieceSize`. Small files with no whole
     piece can't be tested this way.
   - read one piece at a time (`RandomAccessFile`, buffer = piece size), hash with the
     shared `ConcurrentHasher` (SHA-1 for v1, SHA-256 when the stored hash is 32 bytes), compare
     with `torrent.getPieces()[n]`.
   - every piece in range is checked, not a sample.
   - allowed fails = 0, or with tolerance `ceil(tolerance% × len / pieceSize)`. Over the
     limit → candidate rejected (`X`), next candidate. `PIECE` mode never rejects; it just
     doesn't copy the failed piece.
5. First accepted candidate → mode's action, file unskipped and storage set to linear if
   needed. Files without a match go to `unmatched_files` with the candidates that failed.

## Phase 2: name matching

When phase 1 matched some files and some are left (not in `PIECE` mode): from each
matched file and its relative path in the torrent, work out the release's root folder on
disk. If all matches agree on one root, look for each unmatched file at
`root/<relative path>` (any size) and link/copy it too. Catches files that are too small for
a whole piece, or changed. The recheck then finds which of their pieces are good.

## Points worth copying

- Size index first, hashing only same-size candidates: a scan of a big disk costs a
  directory walk, and reads only happen for real candidates.
- The piece hash is the proof; the action is cheap (link) and a full recheck follows.
- Preferring the folder most files were found in.
- Name/path fallback for files with no testable piece.

## In eMule Qt

`Ed3kLocalScan` (ed3k-module.md "Local copies"): size index of the folders against the open
downloads' sizes, part files skipped, the folder with most candidates first, every candidate
hashed whole (BLAKE3 root, no tolerance, no piece mode), a match registered as a found copy;
the download then copies it block by block and verifies as for peers' blocks. No name/path
phase: a root is all or nothing.


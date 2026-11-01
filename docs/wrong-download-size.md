# Wrong download size

Some links in the wild carry size `(n-1) × PARTSIZE` (PARTSIZE = 9,728,000) for a file
that is really larger (ed2k-server issue #40). eMuleQt doesn't make such sizes, but used
to accept them.

## Why nothing noticed

A part-aligned size `k × PARTSIZE` expects `k + 1` part hashes, the last one MD4 of empty
input. That is the same count as any file of up to `(k + 1) × PARTSIZE - 1` bytes. So the
real file's hashset passes the count and root checks, every full part verifies, and the
download could finish as a truncated file shared under the real hash with the wrong size.

## Check

`FileIdentifier::md4TailContradictsSize()`: size part-aligned, hash count as expected,
last hash != `31D6CFE0D16AE931B73C59D7E0C089C0`. `PartFile::checkSizeAgainstHashset()`
also requires the root to match the file hash. Then the size is proven wrong, and the real
size lies in `(size, size + PARTSIZE)`.

Runs when:
- a hashset arrives from a peer (`PartFile::hashsetReceived`);
- a link with a `p=` hashset is added;
- a `.part.met` loads (catches downloads already stuck).

## While in doubt

The download keeps running (its parts 0..k-1 are full parts of the real file too), but:
- `SharedFileList::safeAddKFile` refuses it, so OP_OFFERFILES, Kad publish and browse
  never carry the bad size;
- it never completes;
- its own Kad source search is held.

A finished file with the same contradiction (truncated before this check) isn't shared
either; the log says to delete it and download it again.

## Size lookup

`PartFileSizeLookup` (one per core, one file at a time) asks both networks at once, each
only when enabled (`networkED2K`, `kadEnabled`); a network not yet connected gets 2 min.

- **Server**: internal `ed2k::<hash>` search through the search queue. It waits behind the
  user's server search and is never shown in the GUI (`SearchParams::internal`). Not every
  server supports hash searches.
- **Kad**: a File lookup in size-probe mode asks with size 0, so storing nodes return every
  source record. It counts the `FT_FILESIZE` of each source and takes a size once two
  sources agree, or the most common one when the lookup ends. Sources from it aren't used.

Only answers inside the range count. The first one fixes the download
(`PartFile::applyCorrectedSize`): it grows the file, adds the tail gap, and drops sources
so they're found again with the real size.

When both fail, the file is paused with status Error, and the log + status bar give each
network's reason, with a hint that another server might have it. Resuming looks again.

# ed3k share hashing benchmark

How fast a share list gets its ed3k (BLAKE3) hashes, compared with how v0.7.25 did it.

## What changed

| Version | Existing library (eD2K already hashed) | New file (eD2K + ed3k) |
|---|---|---|
| v0.7.25 | ed3k hashes one file at a time over all disks | read twice: eD2K, then ed3k |
| v0.7.26 | one file per disk, disks in parallel | read once; BLAKE3 fed from eD2K's read on the eD2K thread |
| next | same | same read; BLAKE3 and AICH each on their own thread (`SideHashPipe`, files ≥ 16 MiB) |

- Per-disk queues: `Ed3kShared::m_hashQueues`, keyed by `DiskLoadLimiter::volumeKey`. They run on the
  shared `hashPool()`, which has 4 threads and is also used by joins.
- Single read: `SharedFileList::setSideHasher(Ed3kShared::sideHasherFactory())` →
  `KnownFile::createHash` feeds every read into `SideHash::update`.
- Pipe: `SideHashPipe` (src/core/files/SideHasher.h) works as follows:
  - It copies the reads into 1 MiB chunks, queues at most 8 of them (8 MiB), and hashes them on its own low-priority thread.
  - `finish()` waits for the queue to drain and then finishes on the eD2K thread. So the result is still queued before `hashingFinished`.
  - Below 16 MiB, a thread per file costs more than it saves, so BLAKE3 runs inline.
- AICH: `KnownFile::createFromFile` feeds an `AichSink` (KnownFile.cpp) through a second `SideHashPipe`, files ≥ `KnownFile::aichPipeFrom` (16 MiB).
  - The sink walks the file in order: per part `findHash`, per 180 KiB block `setBlockHash`, per part `reCalculateHash`, the same split as `createHash`.
  - `drain()` waits for it before the file hash and the AICH set are finished. eD2K's thread now runs only the read and MD4.
  - Per new file on a disk: 3 threads (read + MD4, AICH, BLAKE3), 16 MiB of queued chunks at most.

## How to run

The benchmark is `tst_Ed3kShared benchHashRun`. It is skipped unless `EMULE_BENCH_HASH_DIRS` is set.

```sh
EMULE_BENCH_HASH_DIRS=/path/on/disk1:/path/on/disk2 \
EMULE_BENCH_HASH_FILES=4 EMULE_BENCH_HASH_MIB=2048 EMULE_BENCH_HASH_LOAD=100 \
QTEST_FUNCTION_TIMEOUT=3600000 ./build/tests/tst_Ed3kShared benchHashRun
# one row: ./build/tests/tst_Ed3kShared "benchHashRun:new files: one read, side hash own thread"
```

| Env var | Meaning | Default |
|---|---|---|
| `EMULE_BENCH_HASH_DIRS` | one directory per physical disk, `:`-separated | (skip) |
| `EMULE_BENCH_HASH_FILES` | files per disk | 2 |
| `EMULE_BENCH_HASH_MIB` | size of each file | 1024 |
| `EMULE_BENCH_HASH_LOAD` | `hashingDiskLoad` % | 80 |

- Each row writes fresh files with no page cache (F_NOCACHE on macOS, fadvise elsewhere), so every row reads from the disk.
- The v0.7.25 rows emulate the old behaviour in the current build: one volume key for all disks, and no side hash. eD2K still hashes one file per disk in every row.
- Put the directories one level below the drive root, for example `/Volumes/X/emuleqt-bench/`. The temporary folders are removed after each row.
- The output line per row reads `… N GiB in T s → MB/s, ed3k reads R`. R = 0 means ed3k never read a file itself.

| Row | newFiles | perDisk | pipe |
|---|---|---|---|
| library: one at a time (0.7.25) | – | – | – |
| library: one per disk | – | ✔ | – |
| new files: two reads, ed3k one at a time (0.7.25) | ✔ | – | – |
| new files: one read, side hash inline (0.7.26) | ✔ | ✔ | – |
| new files: one read, side hash own thread | ✔ | ✔ | ✔ |
| new files: one read, side hash + AICH own threads | ✔ | ✔ | ✔ + AICH |

## Results (2026-11-13, Apple M3 Max, 128 GB, macOS, load 100 %)

### USB HDD + external SSD, 2 × 512 MiB per disk (2 GiB)

| Case | v0.7.25 | v0.7.26 | BLAKE3 own thread | + AICH own thread |
|---|---|---|---|---|
| existing library | 53.7 s | 30.6 s (×1.75) | (same path) | (same path) |
| new files | 65.5 s | 35.5 s / 32.8 s¹ (×1.85) | 32.5 s / 32.8 s | 32.0 s |

¹ The second value comes from the later run, made alongside the own-thread row.

The HDD is the limit (~35 MB/s). The pipes neither help nor hurt here.

### Internal SSD + external SSD, 4 × 2 GiB per disk (16 GiB)

| Case | v0.7.25 | v0.7.26 | BLAKE3 own thread | + AICH own thread |
|---|---|---|---|---|
| existing library | 17.5 s | 9.8 s (×1.8) | (same path) | (same path) |
| new files | 41.9 / 40.3 / 48.0 s² | 44.9 / 46.1 s² | 38.0 / 37.9 s² | **31.6 / 32.9 s²** (543 MB/s) |

² Separate runs of the same row. The 0.7.25 row varies most: it reads every file twice.

- With BLAKE3 inline, an SSD is limited by the CPU: one thread per disk ran MD4, AICH (SHA-1) and BLAKE3 one after another. That made it slower than v0.7.25, whose two reads ran on two threads.
- With the BLAKE3 pipe, each disk uses two cores, one for MD4/AICH and one for BLAKE3. Each disk now reads once and still beats v0.7.25.
- MD4 + SHA-1 on one thread then capped a disk at about 225 MB/s. With AICH on a third thread a disk reaches about 270 MB/s: ×1.2 vs BLAKE3-only, ×1.4 vs v0.7.26, ×1.3 vs v0.7.25.
- Next limits: SHA-1 alone (one AICH thread per file) and the low-priority threads sharing cores with the rest of the machine.

## Pitfalls

- **Two volumes on one USB bridge are one disk.**
  - The two MyBook Duo volumes read 22–39 MB/s each, but only 33 MB/s together.
  - Per-disk parallelism can't help such a pair, so benchmark on truly independent devices.
  - Check first with two raw parallel reads.
- **Hashing threads run at low priority.** Other load on the machine skews the results.
  - macOS: `QThread::LowPriority` + `IOPOL_UTILITY`.
  - Linux: nice 10 + I/O class BE 7.
- **Results scale with the disk count:**
  - Existing library: ≈ × the number of independent disks (at most 4, the size of `hashPool()`).
  - New files on HDDs: ≈ ×2 per disk (one read instead of two).

#!/usr/bin/env python3
"""
analyze_log.py — Turn an HTTP Cache rig run into a verdict.

Reads `docker compose logs` output from the cachenet rig (one line per event,
prefixed with the container name) and the rig manifest cachenet.py wrote, and
reports what the swarm actually did: what was published, who fetched it, what
the cache server saw, and whether every leecher ended up with the file.

Usage:
    python3 docker/httpcache/analyze_log.py                     # docker/httpcache/nodes.log
    python3 docker/httpcache/analyze_log.py path/to/nodes.log
    python3 docker/httpcache/analyze_log.py --verify            # also sha256 every node's copy
"""

import argparse
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LOG = os.path.join(SCRIPT_DIR, "nodes.log")
RIG_FILE = os.path.join(SCRIPT_DIR, "seed", "rig.json")

NODE_RE = re.compile(r"^(cachenet-(?:node-\d+|cache))\s*\|\s?(.*)$")
NGINX_RE = re.compile(
    r"^(GET|POST|PUT|DELETE|HEAD)\s+(\S+)\s+(\d{3})\s+(\d+)\s+range=\"([^\"]*)\"\s+from=(\S+)")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def node_key(name: str):
    """Sort nodes numerically, with the cache first."""
    if name.endswith("cache"):
        return (0, 0)
    return (1, int(name.rsplit("-", 1)[-1]))


def section(title: str):
    print()
    print("=" * 72)
    print(f"  {title}")
    print("=" * 72)


def subsection(title: str):
    print(f"\n--- {title} ---")


def pct(part: int, whole: int) -> str:
    return f"{(part / whole * 100):.0f}%" if whole else "n/a"


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

class Run:
    """Everything the analyzer extracts, in one pass over the log."""

    def __init__(self, lines, rig):
        self.rig = rig
        self.nodes = set()          # emulecored containers only
        self.cache_lines = []       # (method, path, status, bytes, range, from)
        self.body = defaultdict(list)   # node -> [message, …]

        for raw in lines:
            m = NODE_RE.match(raw.rstrip("\n"))
            if not m:
                continue
            name, msg = m.group(1), m.group(2)
            if name == "cachenet-cache":
                req = NGINX_RE.match(msg.strip())
                if req:
                    self.cache_lines.append((req.group(1), req.group(2), int(req.group(3)),
                                             int(req.group(4)), req.group(5), req.group(6)))
                self.body[name].append(msg)
                continue
            self.nodes.add(name)
            self.body[name].append(msg)

        self.seeders = set(f"cachenet-{n}" for n in rig.get("seeders", []))
        self.leechers = self.nodes - self.seeders

    def matching(self, pattern):
        """Yield (node, match) for every line matching `pattern`."""
        rx = re.compile(pattern)
        for name, msgs in self.body.items():
            for msg in msgs:
                m = rx.search(msg)
                if m:
                    yield name, m

    def nodes_with(self, needle) -> set:
        return {n for n, msgs in self.body.items()
                if n in self.nodes and any(needle in m for m in msgs)}

    def count(self, needle) -> int:
        return sum(m.count(needle) for msgs in self.body.values() for m in msgs)


# ---------------------------------------------------------------------------
# Sections
# ---------------------------------------------------------------------------

def analyze_startup(run: Run):
    section("STARTUP")

    started = run.nodes_with("eMule Core Daemon starting")
    manager = run.nodes_with("HTTP Cache: manager started")

    print(f"Peer containers in log:   {len(run.nodes)}"
          + (f" (manifest says {run.rig.get('peers')})" if run.rig.get("peers") else ""))
    print(f"  seeders:                {len(run.seeders)}  "
          f"{', '.join(sorted(run.seeders, key=node_key))}")
    print(f"  leechers:               {len(run.leechers)}")
    print(f"Daemon started:           {len(started)}")
    print(f"HTTP Cache manager up:    {len(manager)}")

    missing = run.nodes - manager
    if missing:
        print(f"\nWARNING: {len(missing)} peers never started the HTTP Cache manager:")
        for n in sorted(missing, key=node_key):
            print(f"  {n}")

    if run.cache_lines:
        print(f"Cache server requests:    {len(run.cache_lines)}")
    else:
        print("\nWARNING: the cache container logged no HTTP requests at all.")


def analyze_downloads_start(run: Run):
    section("SHARING & DOWNLOAD START")

    hashed = [(n, m) for n, m in run.matching(r"Hashed OK: (\S+) \((\d+) bytes\)")]
    added = run.nodes_with("[entrypoint] Added download link")
    started = {n for n, _ in run.matching(r"Download started: ")}

    wanted = run.rig.get("fileName", "")
    seeder_hashed = {n for n, m in hashed if wanted in m.group(1)}

    print(f"Seeders that hashed {wanted or 'the test file'}: "
          f"{len(seeder_hashed)} / {len(run.seeders)}")
    print(f"Leechers that accepted the link:   {len(added)} / {len(run.leechers)}")
    print(f"Downloads started:                 {len(started)}")

    never = run.leechers - started
    if never:
        print(f"\nWARNING: {len(never)} leechers never started the download — a wrong "
              f"ed2k hash in the link looks exactly like this:")
        for n in sorted(never, key=node_key):
            print(f"  {n}")

    rejected = Counter(m.group(1) for _, m in run.matching(r"Source rejected — (.+?):"))
    if rejected:
        subsection("Sources rejected")
        for reason, count in rejected.most_common():
            print(f"  [{count:4d}x] {reason}")


def analyze_publish(run: Run):
    section("PUBLISH")

    attempts = defaultdict(set)   # node -> {part}
    for n, m in run.matching(r"HTTP Cache: publishing part (\d+) of (.+) for (\d+) peers"):
        attempts[n].add(int(m.group(1)))

    published = defaultdict(dict)  # node -> part -> (bytes, url)
    for n, m in run.matching(r"HTTP Cache: published part (\d+) \((\d+) bytes\) -> (\S+)"):
        published[n][int(m.group(1))] = (int(m.group(2)), m.group(3))

    total_published = sum(len(p) for p in published.values())
    expected = run.rig.get("wholeParts")

    print(f"Publish attempts:  {sum(len(p) for p in attempts.values())} "
          f"from {len(attempts)} peer(s)")
    print(f"Chunks published:  {total_published}"
          + (f"  (the file has {expected} whole parts)" if expected else ""))

    for n in sorted(published, key=node_key):
        parts = ", ".join(str(p) for p in sorted(published[n]))
        print(f"  {n}: parts {parts}")

    group_sizes = [int(m.group(1)) for _, m in
                   run.matching(r"HTTP Cache: publishing part \d+ of .+ for (\d+) peers")]
    if group_sizes:
        print(f"\nPeers waiting on the part at publish time: "
              f"min {min(group_sizes)}, max {max(group_sizes)}, "
              f"avg {sum(group_sizes) / len(group_sizes):.1f}")

    # §4.1: the distinction that matters is scope — one part standing down versus
    # the whole server being off limits.
    part_backoff = [(n, m.group(1), m.group(2)) for n, m in run.matching(
        r"HTTP Cache: publish failed: (.+) — retrying that part in (\d+) min")]
    server_backoff = [(n, m.group(1)) for n, m in run.matching(
        r"HTTP Cache: publish failed: (.+) — pausing all uploads")]

    if part_backoff or server_backoff:
        subsection("Publish failures")
        for n, why, mins in part_backoff:
            print(f"  [part-scoped, {mins} min] {n}: {why}")
        for n, why in server_backoff:
            print(f"  [SERVER-WIDE]            {n}: {why}")
    else:
        print("\nNo publish failures.")

    if run.count("daily publish budget reached"):
        print("NOTE: a peer hit its daily publish budget — maxPublishBytesPerDay is "
              "0 (unlimited) in this rig, so that should not happen.")

    return published


def analyze_fetches(run: Run):
    section("CACHE FETCHES")

    started = defaultdict(set)
    for n, m in run.matching(r"HTTP Cache: fetching part (\d+) of (.+) from (\S+)"):
        started[n].add(int(m.group(1)))

    completed = defaultdict(dict)   # node -> part -> (bytes, connections)
    for n, m in run.matching(
            r"HTTP Cache: part (\d+) complete \((\d+) bytes, (\d+) connection\(s\)\)"):
        completed[n][int(m.group(1))] = (int(m.group(2)), int(m.group(3)))

    total_fetches = sum(len(p) for p in completed.values())
    print(f"Fetches started:   {sum(len(p) for p in started.values())} "
          f"by {len(started)} peer(s)")
    print(f"Parts delivered:   {total_fetches}")

    whole = run.rig.get("wholeParts")
    if whole:
        full = [n for n in run.leechers if len(completed.get(n, {})) >= whole]
        print(f"Leechers that got all {whole} parts over HTTP: "
              f"{len(full)} / {len(run.leechers)}")

    per_part = Counter()
    for parts in completed.values():
        per_part.update(parts.keys())
    if per_part:
        subsection("Deliveries per part")
        for part in sorted(per_part):
            print(f"  part {part}: {per_part[part]} peer(s)")

    resumed = {(n, p): c for n, parts in completed.items()
               for p, (_, c) in parts.items() if c > 1}
    drops = list(run.matching(
        r"HTTP Cache: part (\d+) dropped at (\d+)/(\d+) bytes, resuming in (\d+) ms"))
    if resumed or drops:
        subsection("Resumes")
        print(f"  Connections dropped mid-transfer: {len(drops)}")
        print(f"  Parts that needed more than one connection: {len(resumed)}")
        for (n, p), c in sorted(resumed.items(), key=lambda kv: node_key(kv[0][0]))[:20]:
            print(f"    {n} part {p}: {c} connections")

    short = {n: sorted(started[n] - set(completed.get(n, {})))
             for n in started if started[n] - set(completed.get(n, {}))}
    if short:
        subsection("Fetches started but never completed")
        for n in sorted(short, key=node_key):
            print(f"  {n}: parts {short[n]}")

    return completed


RESULT_NAMES = {0: "Ok", 1: "Disabled", 2: "Busy", 3: "NotWanted",
                4: "BadOffer", 5: "HttpFailed", 6: "SizeMismatch", 7: "Corrupt"}


def analyze_failures(run: Run):
    section("REFUSALS & FAILURES")

    codes = Counter(int(m.group(2)) for _, m in
                    run.matching(r"HTTP Cache: fetch of part (\d+) ended with code (\d+)"))
    if codes:
        subsection("Fetch outcomes (downloader side)")
        for code in sorted(codes):
            print(f"  {RESULT_NAMES.get(code, code):<13} {codes[code]}")

    patterns = [
        ("offer refused (bad URL)",      r"refusing offer from (.+) pointing at"),
        ("offer from a non-source",      r"offered a part of a file it is not a source for"),
        ("ciphertext digest mismatch",   r"ciphertext digest mismatch on part (\d+)"),
        ("bad padding (wrong key)",      r"bad padding on part (\d+)"),
        ("decrypt failed",               r"decrypt failed on part (\d+)"),
        ("wrong decrypted length",       r"decrypted (\d+) bytes, expected"),
        ("server length disagreement",   r"server offers (\d+) bytes, expected"),
        ("server ignored Range",         r"server ignored Range on part (\d+)"),
        ("server rejected Range",        r"server rejected range (\S+) for part"),
        ("unusable Content-Range",       r"unusable Content-Range"),
        ("entry retired (3 strikes)",    r"part (\d+) failed (\d+) times, no longer offered"),
        ("part failed MD4 after fetch",  r"part (\d+) failed its hash after a cache fetch"),
        ("peer banned for corruption",   r"PartFile: banning (\S+) — part (\d+)"),
    ]

    any_hit = False
    subsection("Signals")
    for label, rx in patterns:
        hits = list(run.matching(rx))
        if not hits:
            continue
        any_hit = True
        who = ", ".join(sorted({n for n, _ in hits}, key=node_key)[:6])
        print(f"  [{len(hits):4d}x] {label:<28} {who}")
    if not any_hit:
        print("  None — every offer was accepted and every fetch verified.")

    receipts = list(run.matching(r"HTTP Cache: (.+) fetched part (\d+) \((\d+) bytes\)"))
    print(f"\nOk receipts seen by uploaders: {len(receipts)}")


def analyze_cache_server(run: Run, published, completed):
    section("CACHE SERVER (nginx access log)")

    if not run.cache_lines:
        print("No request lines. Either the run never reached the server, or the "
              "container is not logging to stdout.")
        return

    posts = [r for r in run.cache_lines if r[0] == "POST"]
    gets = [r for r in run.cache_lines if r[0] == "GET" and r[1].startswith("/v1/chunks/")]
    infos = [r for r in run.cache_lines if r[1].rstrip("/").endswith("/v1/info")]
    deletes = [r for r in run.cache_lines if r[0] == "DELETE"]

    ok_posts = [r for r in posts if r[2] == 201]
    ok_gets = [r for r in gets if r[2] in (200, 206)]
    ranged = [r for r in gets if r[4] not in ("", "-")]

    print(f"POST /v1/chunks:        {len(posts)}  ({len(ok_posts)} answered 201)")
    print(f"GET  /v1/chunks/<id>:   {len(gets)}  ({len(ok_gets)} answered 200/206, "
          f"{len(ranged)} carried a Range header)")
    if infos:
        print(f"GET  /v1/info:          {len(infos)}")
    if deletes:
        print(f"DELETE /v1/chunks/<id>: {len(deletes)}")

    bad = Counter(r[2] for r in run.cache_lines if r[2] >= 400)
    if bad:
        subsection("Error responses")
        for status, count in sorted(bad.items()):
            note = {413: "body over client_max_body_size / post_max_size",
                    401: "wrong or missing API key",
                    429: "quota spent",
                    507: "storage floor hit — minFreeBytes should be 0 here",
                    404: "unknown, expired, or a path nginx blocks"}.get(status, "")
            print(f"  {status}: {count}  {note}")

    # The honest arithmetic: the server's own count against what the peers claim.
    claimed_publishes = sum(len(p) for p in published.values())
    claimed_fetches = sum(len(p) for p in completed.values())
    subsection("Cross-check against the peers' own logs")
    print(f"  chunks published, peers said {claimed_publishes:>4} — server saw {len(ok_posts):>4}")
    print(f"  parts fetched,    peers said {claimed_fetches:>4} — server saw {len(ok_gets):>4}")
    if claimed_publishes != len(ok_posts):
        print("  NOTE: a mismatch here means a publish never reached the server, or a "
              "201 was lost on the way back (which strands the chunk until its TTL).")

    # A fetch that keeps re-asking for the same bytes from the same peer is not
    # a resume, it is a stall: the offset never moves. Cheap to spot from here,
    # and invisible in the peers' own logs, which only ever say "fetching part N".
    repeats = Counter((r[1], r[4], r[5]) for r in gets if r[4] not in ("", "-"))
    stuck = sorted((k for k, c in repeats.items() if c > 4), key=str)
    if stuck:
        subsection("Repeated identical range requests")
        print("  Same chunk, same Range, same peer — the stream is not advancing:")
        for key in stuck[:10]:
            path, rng, who = key
            print(f"    [{repeats[key]:5d}x] {who} {path} {rng}")

    # Successful GETs only: a 404 for an id the server never had would
    # otherwise count as a chunk and drag the reuse average down.
    per_chunk = Counter(r[1] for r in ok_gets)
    if per_chunk:
        reuse = sum(per_chunk.values()) / len(per_chunk)
        print(f"\nDistinct chunk ids served: {len(per_chunk)}  "
              f"(each fetched {reuse:.1f} times on average)")
        print("That average IS the feature: every fetch after the first is upstream "
              "the uploader did not have to spend.")


def analyze_completion(run: Run, completed):
    section("COMPLETION")

    done = {n for n, _ in run.matching(r"Download completed: ")}
    expected = len(run.leechers)

    print(f"Downloads completed: {len(done)} / {expected} leechers")
    missing = run.leechers - done
    if missing:
        print(f"\n{len(missing)} leechers had not finished when the log ended:")
        for n in sorted(missing, key=node_key):
            got = len(completed.get(n, {}))
            print(f"  {n}  ({got} part(s) via the cache)")

    whole = run.rig.get("wholeParts") or 0
    size = run.rig.get("fileSize") or 0
    partsize = run.rig.get("partSize") or 0
    if whole and size and done:
        cache_bytes = sum(sum(b for b, _ in completed.get(n, {}).values()) for n in done)
        total_bytes = len(done) * size
        subsection("Offload")
        print(f"  Delivered to finished leechers: {total_bytes:,} bytes")
        print(f"  Of that, over the HTTP cache:   {cache_bytes:,} bytes "
              f"({pct(cache_bytes, total_bytes)})")
        print(f"  Ceiling for this file:          {whole * partsize:,} of {size:,} "
              f"({pct(whole * partsize, size)}) — the short tail part is never published")


def analyze_errors(run: Run):
    section("ERRORS & WARNINGS")

    # Per-connection lifecycle lines. eMule drops idle peer sockets by design, and
    # a swarm that has finished downloading does nothing but that — left in, they
    # bury the two or three lines that actually mean something.
    noise = {
        "pipewire", "locale", "QtMultimedia", "ANSI_X3", "CrashHandler",
        "No QtMultimedia", "for more information", "Previous crash dumps found",
        "Source rejected", "could not add the download link",
        "Socket timed out", "ClientReqSocket::disconnect", "ClientReqSocket error",
        "onSocketError", "Client disconnected", "URLClient disconnected",
        "Remote disconnected",
    }
    errors = []
    for name, msgs in run.body.items():
        for msg in msgs:
            if any(n in msg for n in noise):
                continue
            low = msg.lower()
            if any(kw in low for kw in [
                "error", "crash", "fail", "abort", "segfault", "sigseg", "assert",
                "exception", "timeout", "refused", "could not", "cannot", "unable",
                "invalid", "corrupt", "mismatch", "warning:",
            ]):
                errors.append(f"{name} | {msg}")

    if not errors:
        print("No errors, crashes or warnings found (excluding noise).")
        return

    patterns = Counter()
    for e in errors:
        norm = re.sub(r"cachenet-node-\d+", "NODE", e)
        norm = re.sub(r"\d+\.\d+\.\d+\.\d+", "IP", norm)
        norm = re.sub(r"[0-9a-fA-F]{16,}", "HASH", norm)
        patterns[norm] += 1

    print(f"Unique error patterns: {len(patterns)}")
    for pat, count in patterns.most_common(20):
        print(f"  [{count:3d}x] {pat[:130]}")


def analyze_summary(run: Run, published, completed):
    section("SUMMARY")

    issues = []
    whole = run.rig.get("wholeParts") or 0

    total_published = sum(len(p) for p in published.values())
    if total_published == 0:
        issues.append("CRITICAL: nothing was published — no chunk ever reached the "
                      "cache server, so the feature never ran")
    elif whole and total_published < whole:
        issues.append(f"SIGNIFICANT: only {total_published} of {whole} whole parts were "
                      f"published")

    fetchers = {n for n in completed if completed[n]}
    if not fetchers:
        issues.append("CRITICAL: no peer fetched a part over HTTP — offers were sent but "
                      "never acted on, or never sent")
    elif len(fetchers) < len(run.leechers) * 0.8:
        issues.append(f"SIGNIFICANT: only {len(fetchers)}/{len(run.leechers)} leechers "
                      f"fetched anything from the cache")

    done = {n for n, _ in run.matching(r"Download completed: ")}
    if len(done) < len(run.leechers):
        issues.append(f"INFO: {len(run.leechers) - len(done)} leechers had not finished "
                      f"when the log ended — a longer capture may be all that is missing")

    if run.count("failed its hash after a cache fetch"):
        issues.append("CRITICAL: a part filled from a chunk failed MD4 — the cache "
                      "delivered wrong plaintext")
    if run.count("ciphertext digest mismatch"):
        issues.append("CRITICAL: a ciphertext digest mismatch — the server or the network "
                      "mangled a blob")
    if run.count("no longer offered"):
        issues.append("SIGNIFICANT: an entry was retired after three bad reports")

    server_wide = run.count("pausing all uploads")
    if server_wide:
        issues.append(f"SIGNIFICANT: publishing stood down server-wide {server_wide} time(s)")

    # The stall this rig was written to catch: a fetch re-asking for the same
    # bytes forever. It looks like healthy traffic in every per-node log.
    repeats = Counter((r[1], r[4], r[5]) for r in run.cache_lines
                      if r[0] == "GET" and r[4] not in ("", "-"))
    if repeats and max(repeats.values()) > 4:
        worst = max(repeats.values())
        issues.append(f"CRITICAL: a peer asked for the same range {worst} times — the "
                      f"fetch is not advancing (see the cache server section)")

    bad_status = [r for r in run.cache_lines if r[2] >= 500]
    if bad_status:
        issues.append(f"CRITICAL: the cache server answered {len(bad_status)} request(s) "
                      f"with 5xx")

    crashes = [m for msgs in run.body.values() for m in msgs
               if any(k in m.lower() for k in ["segfault", "sigseg", "assert failed"])
               and "Previous crash dumps" not in m]
    if crashes:
        issues.append(f"CRITICAL: {len(crashes)} crash/assert lines found")

    if not issues:
        print("No major issues detected.")
    else:
        for i, issue in enumerate(issues, 1):
            print(f"  {i}. {issue}")


def verify_copies(run: Run):
    """Hash every node's copy of the file against the manifest.

    The one claim the log cannot make: that the bytes which arrived are the bytes
    that were shared. Needs the containers to still be running.
    """
    section("VERIFY (sha256 inside each container)")

    want = run.rig.get("sha256")
    name = run.rig.get("fileName")
    if not want or not name:
        print("No sha256 in the manifest — run cachenet.py first.")
        return

    ok, wrong, absent = [], [], []
    for node in sorted(run.nodes, key=node_key):
        proc = subprocess.run(
            ["docker", "exec", node, "sha256sum", f"/root/incoming/{name}"],
            capture_output=True, text=True)
        if proc.returncode != 0:
            absent.append(node)
            continue
        got = proc.stdout.split()[0]
        (ok if got == want else wrong).append(node)

    print(f"Matching the shared file: {len(ok)} / {len(run.nodes)}")
    if wrong:
        print(f"\nCRITICAL: {len(wrong)} node(s) hold different bytes under the same name:")
        for n in wrong:
            print(f"  {n}")
    if absent:
        print(f"\n{len(absent)} node(s) have no completed copy (still downloading, or the "
              f"container is gone):")
        for n in absent:
            print(f"  {n}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="Analyze an HTTP Cache rig run")
    ap.add_argument("log", nargs="?", default=DEFAULT_LOG,
                    help=f"log file (default: {DEFAULT_LOG})")
    ap.add_argument("--rig", default=RIG_FILE,
                    help="rig manifest written by cachenet.py")
    ap.add_argument("--verify", action="store_true",
                    help="also sha256 each node's copy of the file (containers must be up)")
    args = ap.parse_args()

    log_path = os.path.abspath(args.log)
    if not os.path.isfile(log_path):
        sys.exit(f"ERROR: log file not found: {log_path}")

    rig = {}
    if os.path.isfile(args.rig):
        with open(args.rig) as f:
            rig = json.load(f)
    else:
        print(f"NOTE: no manifest at {args.rig} — expectations will be inferred from the log.")

    print(f"Analyzing: {log_path}")
    with open(log_path, errors="replace") as f:
        run = Run(f.readlines(), rig)

    if not run.nodes:
        sys.exit("ERROR: no cachenet-node-* lines found. Is this a cachenet log?")

    analyze_startup(run)
    analyze_downloads_start(run)
    published = analyze_publish(run)
    completed = analyze_fetches(run)
    analyze_failures(run)
    analyze_cache_server(run, published, completed)
    analyze_completion(run, completed)
    analyze_errors(run)
    analyze_summary(run, published, completed)
    if args.verify:
        verify_copies(run)
    print()


if __name__ == "__main__":
    main()

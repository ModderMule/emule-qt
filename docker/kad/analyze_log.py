#!/usr/bin/env python3
"""
analyze_log.py — Analyze docker Kad network logs for issues.

Usage:
    python3 docker/kad/analyze_log.py                        # default: docker/kad/nodes.log
    python3 docker/kad/analyze_log.py path/to/nodes.log     # custom log file
"""

import re
import sys
import os
from collections import Counter, defaultdict

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def parse_node(line: str) -> str | None:
    """Extract node name like 'kadnet-node-42' from a log line."""
    m = re.match(r"(kadnet-node-\d+)\s*\|", line)
    return m.group(1) if m else None


def ip_int_to_str(val: int) -> str:
    """Convert a host-order uint32 IP to dotted string."""
    return f"{(val >> 24) & 0xFF}.{(val >> 16) & 0xFF}.{(val >> 8) & 0xFF}.{val & 0xFF}"


# ---------------------------------------------------------------------------
# Section printers
# ---------------------------------------------------------------------------

def section(title: str):
    width = 72
    print()
    print("=" * width)
    print(f"  {title}")
    print("=" * width)


def subsection(title: str):
    print(f"\n--- {title} ---")


# ---------------------------------------------------------------------------
# Analysis functions
# ---------------------------------------------------------------------------

def analyze_startup(lines: list[str], nodes: set[str]):
    section("STARTUP")

    started = {n for l in lines if "eMule Core Daemon starting" in l and (n := parse_node(l))}
    kad_started = {n for l in lines if "Kad: Starting Kademlia" in l and (n := parse_node(l))}
    loaded = {}
    for l in lines:
        m = re.search(r"Loaded nodes\.dat — (\d+) contacts", l)
        if m and (n := parse_node(l)):
            loaded[n] = int(m.group(1))

    print(f"Nodes in log:           {len(nodes)}")
    print(f"Daemon started:         {len(started)}")
    print(f"Kad started:            {len(kad_started)}")
    print(f"Loaded nodes.dat:       {len(loaded)}")
    if loaded:
        counts = Counter(loaded.values())
        for c, num in sorted(counts.items()):
            print(f"  {num} nodes loaded {c} contacts")

    missing_kad = started - kad_started
    if missing_kad:
        print(f"\nWARNING: {len(missing_kad)} nodes started daemon but NOT Kad:")
        for n in sorted(missing_kad):
            print(f"  {n}")

    # WebServer check
    ws_enabled = {n for l in lines if "WebServer: listening" in l and (n := parse_node(l))}
    ws_disabled_cfg = set()
    for l in lines:
        # entrypoint sets webServer.enabled=false but if it still starts...
        pass
    if ws_enabled:
        print(f"\nWebServer listening:    {len(ws_enabled)} nodes")


def analyze_lan_mode(lines: list[str], nodes: set[str]):
    section("LAN MODE")

    lan = {n for l in lines if "Activating LAN Mode" in l and (n := parse_node(l))}
    print(f"Activated LAN mode:     {len(lan)} / {len(nodes)}")
    missing = nodes - lan
    if missing:
        print(f"WARNING: {len(missing)} nodes did NOT activate LAN mode:")
        for n in sorted(missing, key=lambda x: int(x.split("-")[-1])):
            print(f"  {n}")


def analyze_bootstrap(lines: list[str], nodes: set[str]):
    section("BOOTSTRAP")

    bootstrapped = {}
    for l in lines:
        m = re.search(r"Bootstrap complete — connected to the network \((\d+) nodes", l)
        if m and (n := parse_node(l)):
            bootstrapped[n] = int(m.group(1))

    print(f"Bootstrap complete:     {len(bootstrapped)} / {len(nodes)}")
    if bootstrapped:
        counts = Counter(bootstrapped.values())
        for c, num in sorted(counts.items()):
            print(f"  {num} nodes with {c} routing table entries")

    missing = nodes - set(bootstrapped)
    if missing:
        print(f"\nWARNING: {len(missing)} nodes NEVER bootstrapped:")
        for n in sorted(missing, key=lambda x: int(x.split("-")[-1])):
            print(f"  {n}")


def analyze_firewall_tcp(lines: list[str], nodes: set[str]):
    section("FIREWALL CHECK — TCP (FIREWALLED_RES)")

    fw_res = defaultdict(list)
    for l in lines:
        m = re.search(r"FIREWALLED_RES from ([\d.]+) — external IP: ([\d.]+)", l)
        if m and (n := parse_node(l)):
            fw_res[n].append((m.group(1), m.group(2)))

    got_2plus = {n for n, rs in fw_res.items() if len(rs) >= 2}
    got_1 = {n for n, rs in fw_res.items() if len(rs) == 1}
    got_0 = nodes - set(fw_res)

    print(f"Got 2+ FIREWALLED_RES (confirmed open): {len(got_2plus)}")
    print(f"Got exactly 1 (incomplete):             {len(got_1)}")
    print(f"Got 0 (no TCP FW check result):         {len(got_0)}")

    # Check for IP mismatches (different external IPs from different checkers)
    mismatches = {}
    for n, rs in fw_res.items():
        ext_ips = {r[1] for r in rs}
        if len(ext_ips) > 1:
            mismatches[n] = ext_ips
    if mismatches:
        subsection("External IP mismatches")
        for n, ips in sorted(mismatches.items()):
            print(f"  {n}: {', '.join(sorted(ips))}")

    # Check who sent FIREWALLED2_REQ
    sent_req = {n for l in lines if "Sent FIREWALLED2_REQ" in l and (n := parse_node(l))}
    print(f"\nSent FIREWALLED2_REQ:   {len(sent_req)}")

    # Check who handled incoming FIREWALLED2_REQ
    handled_req = {n for l in lines if "FIREWALLED2_REQ from" in l and (n := parse_node(l))}
    print(f"Handled FIREWALLED2_REQ:{len(handled_req)}")


def analyze_firewall_udp(lines: list[str], nodes: set[str]):
    section("FIREWALL CHECK — UDP")

    initiated = defaultdict(int)
    for l in lines:
        if "Initiated UDP FW check via TCP" in l and (n := parse_node(l)):
            initiated[n] += 1

    completed = {}
    for l in lines:
        m = re.search(r"UDP FW check complete — firewalled: (\w+)", l)
        if m and (n := parse_node(l)):
            completed[n] = m.group(1)

    succeeded_count = sum(1 for v in completed.values() if v == "no")
    firewalled_count = sum(1 for v in completed.values() if v == "yes")

    print(f"Nodes that initiated UDP FW check: {len(initiated)}")
    print(f"  Total TCP connections for FW:    {sum(initiated.values())}")
    print(f"Nodes that completed UDP FW check: {len(completed)}")
    print(f"  Not firewalled (good):           {succeeded_count}")
    print(f"  Firewalled (bad):                {firewalled_count}")

    # Individual probe results
    probes_ok = 0
    probes_fail = 0
    for l in lines:
        if "FIREWALLUDP from" in l:
            if "errorCode=0" in l:
                probes_ok += 1
            elif "errorCode=1" in l:
                probes_fail += 1
    print(f"\nUDP FW probes received:            {probes_ok + probes_fail}")
    print(f"  errorCode=0 (success):           {probes_ok}")
    print(f"  errorCode=1 (failed):            {probes_fail}")

    not_completed = set(initiated) - set(completed)
    if not_completed:
        print(f"\nWARNING: {len(not_completed)} nodes initiated but never completed UDP FW check:")
        for n in sorted(not_completed, key=lambda x: int(x.split("-")[-1])):
            print(f"  {n} ({initiated[n]} TCP connections)")


def analyze_searches(lines: list[str], nodes: set[str]):
    section("KAD SEARCHES")

    searches = defaultdict(lambda: defaultdict(int))  # node -> search_type -> count
    for l in lines:
        m = re.search(r"Started (\w+(?:\s+\w+)*) search (\d+) for", l)
        if m and (n := parse_node(l)):
            search_type = m.group(1)
            search_num = int(m.group(2))
            searches[n][search_type] = max(searches[n][search_type], search_num)

    # Count by search type
    type_counts = defaultdict(lambda: Counter())
    for n, types in searches.items():
        for stype, max_num in types.items():
            type_counts[stype][max_num] += 1

    for stype in sorted(type_counts):
        subsection(f"Search type: {stype}")
        total = sum(type_counts[stype].values())
        print(f"  Nodes that started at least one: {total}")
        for num, count in sorted(type_counts[stype].items()):
            print(f"  Reached search #{num}: {count} nodes")

    # Convergence
    converged = []
    for l in lines:
        m = re.search(r"search (\d+): converged — best=(\d+) responded=(\d+) possible=(\d+) tried=(\d+)", l)
        if m and (n := parse_node(l)):
            converged.append({
                "node": n,
                "search": int(m.group(1)),
                "best": int(m.group(2)),
                "responded": int(m.group(3)),
                "possible": int(m.group(4)),
                "tried": int(m.group(5)),
            })

    subsection("Search convergence")
    print(f"  Total converged searches: {len(converged)}")
    for c in converged:
        print(f"  {c['node']} search #{c['search']}: "
              f"best={c['best']} responded={c['responded']} "
              f"possible={c['possible']} tried={c['tried']}")

    no_search = nodes - set(searches)
    if no_search:
        print(f"\n  WARNING: {len(no_search)} nodes never started any search")


def analyze_publish(lines: list[str], nodes: set[str]):
    section("KAD PUBLISHING")

    publish_patterns = [
        "PUBLISH", "publish", "STORE_REQ", "StoreKey", "StoreNote",
        "StoreSource", "KADEMLIA2_PUBLISH", "publishFile", "publishKeyword",
        "publishSource", "Started StoreKeyword", "Started StoreFile",
        "Started StoreNote", "Started FindValue",
    ]

    found = defaultdict(list)
    for l in lines:
        for pat in publish_patterns:
            if pat in l and (n := parse_node(l)):
                found[pat].append(n)
                break

    if not found:
        print("*** NO PUBLISH ACTIVITY DETECTED ***")
        print("This is the most critical issue — without publishing,")
        print("the DHT has content in routing tables but no file data.")
        print()
        print("Possible causes:")
        print("  1. Publish requires isConnected() which needs completed FW checks")
        print("  2. Publish timer hasn't fired (may need longer run time)")
        print("  3. Publish logic not implemented or not triggered")
    else:
        for pat, node_list in sorted(found.items()):
            unique = set(node_list)
            print(f"  '{pat}': {len(node_list)} occurrences from {len(unique)} nodes")


def analyze_connections(lines: list[str], nodes: set[str]):
    section("TCP CONNECTIONS")

    encrypted = 0
    unencrypted = 0
    no_hash = 0
    for l in lines:
        if "encrypted=1" in l and "connectToHost" in l:
            encrypted += 1
        elif "encrypted=0" in l and "connectToHost" in l:
            unencrypted += 1
        if "hasValidHash=0" in l:
            no_hash += 1

    connected = 0
    for l in lines:
        if "EMSocket::onConnected" in l:
            connected += 1

    print(f"TCP connect attempts:   {encrypted + unencrypted}")
    print(f"  Encrypted:            {encrypted}")
    print(f"  Unencrypted:          {unencrypted}")
    print(f"  No valid hash:        {no_hash}")
    print(f"TCP connected (success):{connected}")

    if encrypted + unencrypted > 0:
        pct = unencrypted / (encrypted + unencrypted) * 100
        if pct > 20:
            print(f"\nWARNING: {pct:.0f}% of TCP connections are unencrypted")
            print("  (hasValidHash=0 means peer's user hash not known at connect time)")


def analyze_secure_ident(lines: list[str], nodes: set[str]):
    section("SECURE IDENTIFICATION")

    # --- Initialization ---
    rsa_init = {n for l in lines if "RSA secure identification initialized" in l and (n := parse_node(l))}
    rsa_disabled = {n for l in lines if "Secure identification disabled" in l and (n := parse_node(l))}
    rsa_failed = {n for l in lines if "key pair generation failed" in l and (n := parse_node(l))}

    subsection("Initialization")
    print(f"RSA initialized:        {len(rsa_init)} / {len(nodes)}")
    if rsa_disabled:
        print(f"SecureIdent disabled:   {len(rsa_disabled)}")
    if rsa_failed:
        print(f"Key generation failed:  {len(rsa_failed)}")

    # --- Handshake flow ---
    send_state = defaultdict(int)
    recv_state = defaultdict(int)
    recv_credits = Counter()
    send_pubkey = defaultdict(int)
    recv_pubkey = defaultdict(int)
    send_sig_deferred = defaultdict(int)
    recv_sig = defaultdict(int)
    verified_ok = defaultdict(int)
    verified_fail = defaultdict(int)
    verify_calls = 0
    rsa_verify_failed = 0
    key_decode_failed = 0

    for l in lines:
        n = parse_node(l)
        if not n:
            continue

        if "sendSecIdentStatePacket:" in l:
            send_state[n] += 1
        elif "processSecIdentStatePacket:" in l:
            recv_state[n] += 1
            m = re.search(r"credits=(\w+)", l)
            if m:
                recv_credits[m.group(1)] += 1
        elif "sendPublicKeyPacket:" in l:
            send_pubkey[n] += 1
        elif "processPublicKeyPacket:" in l:
            recv_pubkey[n] += 1
        elif "no remote public key yet" in l:
            send_sig_deferred[n] += 1
        elif "processSignaturePacket:" in l:
            recv_sig[n] += 1
            m = re.search(r"verified=(\d)", l)
            if m:
                if m.group(1) == "1":
                    verified_ok[n] += 1
                else:
                    verified_fail[n] += 1
        elif "verifyIdent:" in l:
            verify_calls += 1
            if "RSA_verify failed" in l:
                rsa_verify_failed += 1
            elif "all key decoders failed" in l:
                key_decode_failed += 1

    subsection("Handshake flow")
    print(f"sendSecIdentState:      {sum(send_state.values()):>5}  ({len(send_state)} nodes)")
    print(f"processSecIdentState:   {sum(recv_state.values()):>5}  ({len(recv_state)} nodes)")
    if recv_credits:
        parts = ", ".join(f"{k}={v}" for k, v in sorted(recv_credits.items()))
        print(f"  credits:              {parts}")
    print(f"sendPublicKey:          {sum(send_pubkey.values()):>5}  ({len(send_pubkey)} nodes)")
    print(f"processPublicKey:       {sum(recv_pubkey.values()):>5}  ({len(recv_pubkey)} nodes)")
    print(f"sendSignature deferred: {sum(send_sig_deferred.values()):>5}  ({len(send_sig_deferred)} nodes)")

    subsection("Verification")
    total_verified = sum(verified_ok.values()) + sum(verified_fail.values())
    ok = sum(verified_ok.values())
    fail = sum(verified_fail.values())
    rate = (ok / total_verified * 100) if total_verified > 0 else 0

    print(f"processSignaturePacket: {sum(recv_sig.values()):>5}  ({len(recv_sig)} nodes)")
    print(f"  verified=1 (success): {ok}")
    print(f"  verified=0 (failure): {fail}")
    print(f"verifyIdent calls:      {verify_calls}")
    print(f"  RSA_verify failed:    {rsa_verify_failed}")
    if key_decode_failed:
        print(f"  key decode failed:    {key_decode_failed}")
    print(f"\nSuccess rate:           {ok}/{total_verified} ({rate:.0f}%)")

    if total_verified > 0 and ok == 0:
        print("\nCRITICAL: Every identity verification failed!")
        print("  All nodes exchange keys but RSA_verify rejects every signature.")
        print("  Likely cause: signing/verification mismatch (message format, padding, or endianness)")


def analyze_errors(lines: list[str]):
    section("ERRORS & WARNINGS")

    noise = {
        "pipewire", "locale", "QtMultimedia", "ANSI_X3", "CrashHandler",
        "No QtMultimedia", "for more information", "FIREWALLUDP",
        "errorCode=",
    }
    errors = []
    for l in lines:
        if any(n in l for n in noise):
            continue
        lower = l.lower()
        if any(kw in lower for kw in [
            "error", "crash", "fail", "abort", "segfault", "sigseg",
            "assert", "exception", "timeout", "refused", "could not",
            "cannot", "unable", "invalid", "corrupt", "mismatch",
            "warning:", "bad ",
        ]):
            errors.append(l.rstrip())

    if not errors:
        print("No errors, crashes, or warnings found (excluding noise).")
    else:
        # Deduplicate by pattern
        patterns = Counter()
        for e in errors:
            # Normalize node name and IPs for dedup
            normalized = re.sub(r"kadnet-node-\d+", "NODE", e)
            normalized = re.sub(r"\d+\.\d+\.\d+\.\d+", "IP", normalized)
            normalized = re.sub(r"[0-9a-fA-F]{16,}", "HASH", normalized)
            patterns[normalized] += 1

        print(f"Unique error patterns: {len(patterns)}")
        for pat, count in patterns.most_common(20):
            print(f"  [{count:3d}x] {pat[:120]}")


def analyze_routing_table(lines: list[str], nodes: set[str]):
    section("ROUTING TABLE GROWTH")

    # KADEMLIA2_RES responses carry contacts that grow the routing table
    res_counts = defaultdict(int)
    res_sizes = defaultdict(list)
    for l in lines:
        m = re.search(r"KADEMLIA2_RES from .+, (\d+) bytes", l)
        if m and (n := parse_node(l)):
            res_counts[n] += 1
            res_sizes[n].append(int(m.group(1)))

    print(f"Nodes receiving KADEMLIA2_RES: {len(res_counts)}")
    if res_counts:
        vals = list(res_counts.values())
        print(f"  Min responses per node:      {min(vals)}")
        print(f"  Max responses per node:      {max(vals)}")
        print(f"  Avg responses per node:      {sum(vals) / len(vals):.1f}")
        print(f"  Total responses:             {sum(vals)}")

    # HELLO_REQ / HELLO_RES activity (initial contact)
    hello_req = sum(1 for l in lines if "sending HELLO_REQ" in l)
    hello_res = sum(1 for l in lines if "HELLO_RES triggered" in l or "HELLO_REQ_ACK triggered" in l)
    print(f"\nHELLO_REQ sent:         {hello_req}")
    print(f"HELLO_RES/ACK received: {hello_res}")


def analyze_timing(lines: list[str], nodes: set[str]):
    section("TIMING ANALYSIS")

    # Estimate ordering from log: find first and last Bootstrap complete
    first_bootstrap = None
    last_bootstrap = None
    first_search = None
    last_search = None
    bootstrap_idx = []
    search_idx = []

    for i, l in enumerate(lines):
        if "Bootstrap complete" in l:
            bootstrap_idx.append(i)
            if first_bootstrap is None:
                first_bootstrap = parse_node(l)
            last_bootstrap = parse_node(l)
        if "Started Node search 1" in l:
            search_idx.append(i)
            if first_search is None:
                first_search = parse_node(l)
            last_search = parse_node(l)

    if bootstrap_idx:
        print(f"First bootstrap at line {bootstrap_idx[0]:>6} ({first_bootstrap})")
        print(f"Last  bootstrap at line {bootstrap_idx[-1]:>6} ({last_bootstrap})")
        print(f"  Span: {bootstrap_idx[-1] - bootstrap_idx[0]} lines")
    if search_idx:
        print(f"First search 1  at line {search_idx[0]:>6} ({first_search})")
        print(f"Last  search 1  at line {search_idx[-1]:>6} ({last_search})")
        print(f"  Span: {search_idx[-1] - search_idx[0]} lines")

    print(f"\nTotal log lines: {len(lines)}")


def analyze_summary(lines: list[str], nodes: set[str]):
    section("SUMMARY")

    issues = []

    # Check bootstrap
    bootstrapped = {n for l in lines if "Bootstrap complete" in l and (n := parse_node(l))}
    if len(bootstrapped) < len(nodes):
        issues.append(f"CRITICAL: {len(nodes) - len(bootstrapped)} nodes never bootstrapped")

    # Check publish
    has_publish = any(
        kw in l for l in lines
        for kw in ["PUBLISH", "StoreKey", "StoreSource", "StoreNote", "publishFile"]
    )
    if not has_publish:
        issues.append("CRITICAL: No Kad publish activity — DHT has no file content")

    # Check search progress
    search1_nodes = {n for l in lines if "Started Node search 1" in l and (n := parse_node(l))}
    search2_nodes = {n for l in lines if re.search(r"Started Node search [2-9]", l) and (n := parse_node(l))}
    if len(search1_nodes) < len(nodes) * 0.8:
        issues.append(f"SIGNIFICANT: Only {len(search1_nodes)}/{len(nodes)} nodes started first random lookup")
    if len(search2_nodes) < len(nodes) * 0.3:
        issues.append(f"SIGNIFICANT: Only {len(search2_nodes)}/{len(nodes)} nodes reached second random lookup")

    # Check FW
    fw_complete = {n for l in lines if "UDP FW check complete" in l and (n := parse_node(l))}
    if len(fw_complete) < len(nodes) * 0.3:
        issues.append(f"SIGNIFICANT: Only {len(fw_complete)}/{len(nodes)} nodes completed UDP FW check")

    # Check SecureIdent
    sig_ok = sum(1 for l in lines if "verified=1" in l and "processSignaturePacket:" in l)
    sig_fail = sum(1 for l in lines if "verified=0" in l and "processSignaturePacket:" in l)
    sig_total = sig_ok + sig_fail
    if sig_total > 0 and sig_ok == 0:
        issues.append(f"CRITICAL: SecureIdent — all identity verifications failed ({sig_fail}/{sig_total})")
    elif sig_total > 0 and sig_ok / sig_total < 0.8:
        issues.append(f"SIGNIFICANT: SecureIdent success rate only {sig_ok/sig_total*100:.0f}% ({sig_ok}/{sig_total})")

    # Check unencrypted
    unenc = sum(1 for l in lines if "encrypted=0" in l and "connectToHost" in l)
    total_tcp = sum(1 for l in lines if "connectToHost" in l and "encrypted=" in l)
    if total_tcp > 0 and unenc / total_tcp > 0.2:
        issues.append(f"MINOR: {unenc}/{total_tcp} TCP connections unencrypted ({unenc/total_tcp*100:.0f}%)")

    # Check errors
    noise = {"pipewire", "locale", "QtMultimedia", "ANSI_X3", "CrashHandler", "No QtMultimedia"}
    real_errors = [
        l for l in lines
        if any(kw in l.lower() for kw in ["crash", "segfault", "sigseg", "abort", "assert"])
        and not any(n in l for n in noise)
    ]
    if real_errors:
        issues.append(f"CRITICAL: {len(real_errors)} crash/assert/segfault lines found")

    if not issues:
        print("No major issues detected.")
    else:
        for i, issue in enumerate(issues, 1):
            print(f"  {i}. {issue}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) > 1:
        log_path = sys.argv[1]
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        log_path = os.path.join(script_dir, "nodes.log")

    log_path = os.path.abspath(log_path)
    if not os.path.isfile(log_path):
        print(f"ERROR: Log file not found: {log_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Analyzing: {log_path}")

    with open(log_path, "r", errors="replace") as f:
        lines = f.readlines()

    # Discover all nodes
    nodes: set[str] = set()
    for l in lines:
        n = parse_node(l)
        if n:
            nodes.add(n)

    if not nodes:
        print("ERROR: No kadnet-node-* entries found in log. Is this a docker kad log?")
        sys.exit(1)

    analyze_startup(lines, nodes)
    analyze_lan_mode(lines, nodes)
    analyze_bootstrap(lines, nodes)
    analyze_firewall_tcp(lines, nodes)
    analyze_firewall_udp(lines, nodes)
    analyze_searches(lines, nodes)
    analyze_publish(lines, nodes)
    analyze_routing_table(lines, nodes)
    analyze_connections(lines, nodes)
    analyze_secure_ident(lines, nodes)
    analyze_errors(lines)
    analyze_timing(lines, nodes)
    analyze_summary(lines, nodes)

    print()


if __name__ == "__main__":
    main()
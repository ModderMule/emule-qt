# Kad docker compose network for debugging

### run and print logs

indefinitely:
```bash
python3 docker/kad/kadnet.py --down && python3 docker/kad/kadnet.py --nodes 100 --build && clear && python3 docker/kad/kadnet.py --logs 
```

5min to file:
```bash
python3 docker/kad/kadnet.py --down && python3 docker/kad/kadnet.py --nodes 100 --build && clear && timeout 300 python3 docker/kad/kadnet.py --logs 2>&1 | tee docker/kad/nodes.log; python3 docker/kad/kadnet.py --down
```

### analyze logs
```bash
python3 docker/kad/analyze_log.py docker/kad/nodes.log
```

### network monitor
The image always ships `ss`, `nstat`, `tcpdump` and `docker/netmon.sh`; nothing runs unless asked for:
```bash
python3 docker/kad/kadnet.py --nodes 100 --netmon
```
Each node then writes to `docker/kad/netmon/node-N/`:
- `syn.txt` — every TCP SYN/FIN/RST on the daemon port, same clock as the daemon log
- `samples.txt` — every 2 s: socket states, accept queue, kernel TCP counters, ARP table fill, load, daemon CPU ticks

Watch `neigh=entries/thresh3,fulls:N`: the ARP table is one per Docker host, shared by all containers.
With ~100 nodes it overflows the default 1024 entries and first packets to a new peer are dropped (stalled TCP connects, lost Kad packets).

### ARP table limit
Above ~30 nodes, start the rig with `--raise-arp-limit`. It runs one privileged container that raises the host limit; the setting lasts until Docker restarts.
```bash
python3 docker/kad/kadnet.py --nodes 100 --raise-arp-limit
```

### runtime options
The entrypoint is bind-mounted and every node option is a container variable, so only daemon
code changes need `--build`. Variables (see `entrypoint.sh`): `FIREWALL4`, `FIREWALL6`,
`PUBLIC_IPV6`, `NAT_TRAVERSAL`, `LOG_TO_DISK`, `UNLIMITED_BANDWIDTH`, `SKIP_LAN_FW_CHECKS`.
```bash
python3 docker/kad/kadnet.py --nodes 20 --ipv6 --firewall 5,6   # nodes 5 and 6 behind a NAT
```
`--firewall` makes a node a NAT: inbound IPv4 TCP and UDP on its ports pass only as replies
to its own traffic, so it fails both Kad firewall checks and needs a buddy.

### Kad buddy NAT rendezvous test
```bash
python3 docker/kad/kadnet.py --nat-test            # 20 nodes, 25 MiB, tears down after
python3 docker/kad/kadnet.py --nat-test --build --keep
```
- The last two nodes are firewalled: node N-1 shares a random file behind a NAT and an IPv6
  firewall (inbound v6 TCP refused), node N downloads it behind a NAT. The rest are open.
- The script waits for node N-1's buddy and its source publish with it (~5.5 min; the first
  buddy search starts 5 min after Kad start), then adds the link on node N.
- Node N's direct IPv6 dial is refused, it falls back to the source's buddy, the buddy
  introduces the two, node N-1 dials node N over IPv6 (handoff) and uploads.
- Checks: fallback line, `via Kad buddy`, the handoff dial and the MD5. Prints PASS/FAIL.
  Passed with 20 and 32 nodes: ~5.5 s from adding the link to the verified file.
- Not fewer than 20 nodes: in a smaller LAN every node knows every other, every UDP firewall
  check ends "cancelled", and an unverified node refuses buddy requests (10 nodes: no buddy).
  `SKIP_LAN_FW_CHECKS` does not help — it skips the TCP check too, and the buddy handler
  then still sees "firewalled" (as in MFC).
- Logs: `emulecored.log`, `_Verbose.log`, `_Kad.log` in each node's `/root/eMuleQt/Config`
  (`--keep` to read them). Protocol: `docs/protocol/natt-rendezvous.md`.

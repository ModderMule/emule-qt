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

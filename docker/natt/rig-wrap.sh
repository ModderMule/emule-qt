#!/bin/sh
# Rig entrypoint: force a LowID, capture traffic, then the official entrypoint.
set -eu

# The server tests reachability by dialing our IPv4 listen port; refuse it at once.
# Peers still reach us over IPv6, which is what the rendezvous hands over.
iptables -A INPUT -p tcp -s "$SERVER_IP" --dport "${EMULEQT_TCP_PORT:-4662}" \
    -j REJECT --reject-with tcp-reset

# Unbuffered (-U) so the capture can be read while the node runs
tcpdump -U -i eth0 -n -w /config/capture.pcap >/dev/null 2>&1 &

exec /usr/local/bin/emuleqt-entrypoint "$@"

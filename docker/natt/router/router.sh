#!/bin/sh
# Rig router: masquerade LAN -> public, let only replies back in, capture both sides.
# Env: PUB_IP, LAN_IP, LAN_SUBNET, NAT_RANDOM (1 = symmetric). Writes to /cap.
set -eu

# Interface holding an address (docker picks the names by attach order)
iface_of() {
    ip -o -4 addr show | awk -v a="$1" '{ split($4, p, "/"); if (p[1] == a) print $2 }' \
        | sed 's/@.*//' | head -n 1
}

pub_if=$(iface_of "$PUB_IP")
lan_if=$(iface_of "$LAN_IP")
[ -n "$pub_if" ] && [ -n "$lan_if" ] || { echo "router: no interface for $PUB_IP / $LAN_IP" >&2; ip -o -4 addr show >&2; exit 1; }

random=""
[ "${NAT_RANDOM:-0}" = "1" ] && random="--random-fully"

# Unsolicited input to the router itself is dropped, as on a home router. Accepted, it
# would confirm a conntrack entry for the peer's early punch, and our own mapping for
# that peer would then clash with it and get a new port (not the one the server saw).
iptables -A INPUT -i "$pub_if" -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
iptables -A INPUT -i "$pub_if" -j DROP
iptables -P FORWARD DROP
iptables -A FORWARD -i "$lan_if" -o "$pub_if" -j ACCEPT
iptables -A FORWARD -i "$pub_if" -o "$lan_if" -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
# shellcheck disable=SC2086
iptables -t nat -A POSTROUTING -s "$LAN_SUBNET" -o "$pub_if" -j MASQUERADE $random

# Unbuffered (-U): readable while the rig runs; -Z root: keep writing to the bind mount
tcpdump -U -Z root -n -i "$pub_if" -w /cap/pub.pcap >/dev/null 2>&1 &
tcpdump -U -Z root -n -i "$lan_if" -w /cap/lan.pcap >/dev/null 2>&1 &

{
    echo "pub=$pub_if lan=$lan_if nat=${random:-cone}"
    iptables -S
    iptables -t nat -S
} > /cap/rules.txt
sleep 1
touch /cap/ready

trap 'kill $(jobs -p) 2>/dev/null; exit 0' TERM INT
while :; do sleep 3600 & wait $!; done

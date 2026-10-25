#!/bin/sh
# Rig entrypoint: force a LowID, capture traffic, then the official entrypoint.
set -eu

# --nat real|symmetric: the node sits on a LAN behind a rig router (RIG_DEFAULT_ROUTE)
if [ -n "${RIG_DEFAULT_ROUTE:-}" ]; then
    ip route replace default via "$RIG_DEFAULT_ROUTE"
fi

# The servers test reachability by dialing our IPv4 listen port; refuse it at once.
# Peers still reach us over IPv6, which is what the rendezvous hands over.
# SERVER_IP may list several servers, space separated.
for ip in ${SERVER_IP:-}; do
    iptables -A INPUT -p tcp -s "$ip" --dport "${EMULEQT_TCP_PORT:-4662}" \
        -j REJECT --reject-with tcp-reset
done

# Optional "ip:port ..." we may not log in to (two-server mode). UDP stays open.
for target in ${RIG_REJECT_TCP:-}; do
    iptables -A OUTPUT -p tcp -d "${target%:*}" --dport "${target##*:}" \
        -j REJECT --reject-with tcp-reset
done

# Unbuffered (-U) so the capture can be read while the node runs
tcpdump -U -i eth0 -n -w /config/capture.pcap >/dev/null 2>&1 &

exec /usr/local/bin/emuleqt-entrypoint "$@"

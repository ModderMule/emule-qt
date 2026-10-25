# ---------------------------------------------------------------------------
# NAT rendezvous rig router (natnet.py --nat real|symmetric)
#
# Linux conntrack MASQUERADE between a LAN and the rig's public net: port-restricted
# cone, or symmetric with NAT_RANDOM=1 (--random-fully). Captures the public side.
# ---------------------------------------------------------------------------
FROM alpine:3.20

RUN apk add --no-cache iptables tcpdump iproute2

COPY router.sh /usr/local/bin/router
RUN chmod +x /usr/local/bin/router

ENTRYPOINT ["/usr/local/bin/router"]

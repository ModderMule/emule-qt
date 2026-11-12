# ---------------------------------------------------------------------------
# ed3k Kad rig node (ed3knet.py): packmule-node, mounted at /opt/packmule-node
#
# RIG_DEFAULT_ROUTE: default route via a NAT router container (needs NET_ADMIN).
# ---------------------------------------------------------------------------
FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends iproute2 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY entrypoint.sh /usr/local/bin/ed3k-node
RUN chmod +x /usr/local/bin/ed3k-node

ENTRYPOINT ["/usr/local/bin/ed3k-node"]

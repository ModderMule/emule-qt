# ---------------------------------------------------------------------------
# NAT rendezvous rig daemon: the official image plus iptables and tcpdump
#
# Built by natnet.py (context: docker/natt) on top of an image from docker/emuleqt/Dockerfile (BASE).
# rig-wrap.sh refuses the server's IPv4 dial-back so every node is a LowID.
# ---------------------------------------------------------------------------
ARG BASE=emuleqt:natnet-base
FROM ${BASE}

RUN apt-get update && apt-get install -y --no-install-recommends iptables tcpdump iproute2 \
    && rm -rf /var/lib/apt/lists/*

COPY rig-wrap.sh /usr/local/bin/rig-wrap
RUN chmod +x /usr/local/bin/rig-wrap

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/rig-wrap"]

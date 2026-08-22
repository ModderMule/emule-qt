# ---------------------------------------------------------------------------
# daemon.Dockerfile — emulecored daemon image, shared by every Docker test rig
#
# Usage:
#   docker build -f docker/daemon.Dockerfile -t emuleqt-daemon .
#
# Builds only the headless daemon (no GUI) in a minimal runtime image.
#
# Two rigs run on this image: docker/kad (Kademlia LAN network) and
# docker/httpcache (HTTP Cache swarm). The Kad entrypoint is baked in below;
# the HTTP Cache rig bind-mounts its own and overrides `entrypoint:` in its
# compose file, so a rig's behaviour can be edited without a rebuild.
# ---------------------------------------------------------------------------

# === Stage 1: Build =========================================================
FROM --platform=linux/amd64 debian:trixie AS builder

ENV DEBIAN_FRONTEND=noninteractive

# System build dependencies (headless — skip X11/GL/Wayland libs)
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        curl \
        git \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
        zlib1g-dev \
        libssl-dev \
        libglib2.0-dev \
        libdbus-1-dev \
        libpulse-dev \
        libfontconfig1-dev \
        libfreetype6-dev \
        libegl-dev \
        libgl-dev \
        libxkbcommon-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Qt 6.10.2 via aqtinstall
RUN python3 -m venv /opt/aqt-venv \
    && /opt/aqt-venv/bin/pip install aqtinstall \
    && /opt/aqt-venv/bin/aqt install-qt linux desktop 6.10.2 linux_gcc_64 \
        -m qtmultimedia qthttpserver qtwebsockets \
        --base https://ftp.fau.de/qtproject/ \
        --outputdir /opt/Qt \
    && rm -rf /opt/aqt-venv

ENV CMAKE_PREFIX_PATH=/opt/Qt/6.10.2/gcc_64
ENV PATH="/opt/Qt/6.10.2/gcc_64/bin:${PATH}"
ENV LD_LIBRARY_PATH=/opt/Qt/6.10.2/gcc_64/lib

# Copy source and build only the daemon target
WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    && cmake --build build --target emulecored --parallel "$(nproc)"

# === Stage 2: Minimal runtime ===============================================
FROM --platform=linux/amd64 debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive

# Runtime dependencies only
RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3t64 \
        zlib1g \
        libglib2.0-0t64 \
        libdbus-1-3 \
        libpulse0 \
        libfontconfig1 \
        libfreetype6 \
        libicu76 \
        libpcre2-16-0 \
        libdouble-conversion3 \
        libb2-1 \
        libgl1 \
        libxkbcommon0 \
        libegl1 \
        libgssapi-krb5-2 \
        binutils \
    && rm -rf /var/lib/apt/lists/*

# Copy Qt shared libs from builder
COPY --from=builder /opt/Qt/6.10.2/gcc_64/lib /opt/qt/lib
ENV LD_LIBRARY_PATH=/opt/qt/lib

# Copy daemon binary
COPY --from=builder /src/build/src/daemon/emulecored /usr/local/bin/emulecored

# Copy bundled data (nodes.dat template, server.met)
COPY --from=builder /src/data/config /usr/local/share/emuleqt/config

# Verify no missing shared libraries (fail build early instead of at runtime)
RUN ldd /usr/local/bin/emulecored | grep "not found" && exit 1 || true

# Copy entrypoint
COPY docker/kad/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]

# ---------------------------------------------------------------------------
# Official eMuleQt image: headless core, optional GUI in the browser (noVNC)
#
# Build (from project root, submodules checked out):
#   docker build -f docker/emuleqt/Dockerfile -t emuleqt .
#
# Runs emulecored on /config + /data. EMULEQT_GUI=1 also runs the Qt GUI on a
# virtual display, viewed at http://<host>:5800. See docs/docker.md.
#
# Multi-arch: linux/amd64 and linux/arm64 (TARGETARCH picks the Qt build).
# ---------------------------------------------------------------------------

ARG QT_VERSION=6.10.3

# === Stage 1: Build =========================================================
FROM debian:trixie AS builder

ARG QT_VERSION
ARG TARGETARCH
# 1: ed3k node locked off (early alpha), read by CMakeLists.txt; docker.yml sets it
ARG EMULE_ED3K_LOCKED=
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates file \
        pkg-config protobuf-compiler squashfs-tools \
        python3 python3-venv \
        zlib1g-dev libssl-dev libboost-dev libglib2.0-dev libdbus-1-dev libpulse-dev \
        libgl1-mesa-dev libegl1-mesa-dev libvulkan-dev \
        libfontconfig1-dev libfreetype6-dev libwayland-dev \
        libxkbcommon-dev libxkbcommon-x11-dev \
        libxcb1-dev libxcb-cursor-dev libxcb-icccm4-dev libxcb-image0-dev \
        libxcb-keysyms1-dev libxcb-render-util0-dev libxcb-shape0-dev \
        libxcb-xinerama0-dev libxcb-xkb-dev libxcb-randr0-dev \
        libx11-dev libx11-xcb-dev libxext-dev libxrender-dev libxrandr-dev \
        libxfixes-dev libxi-dev libxcursor-dev libsm-dev libice-dev \
    && rm -rf /var/lib/apt/lists/*

# Qt via aqtinstall; HttpServer needs WebSockets, qtgrpc ships QtProtobuf (protoc from apt)
RUN case "$TARGETARCH" in \
        amd64) AQT_HOST=linux;       AQT_ARCH=linux_gcc_64;    QT_DIR=gcc_64 ;; \
        arm64) AQT_HOST=linux_arm64; AQT_ARCH=linux_gcc_arm64; QT_DIR=gcc_arm64 ;; \
        *) echo "unsupported TARGETARCH=$TARGETARCH" >&2; exit 1 ;; \
    esac \
    && python3 -m venv /opt/aqt-venv \
    && /opt/aqt-venv/bin/pip install --no-cache-dir aqtinstall \
    && /opt/aqt-venv/bin/aqt install-qt "$AQT_HOST" desktop "$QT_VERSION" "$AQT_ARCH" \
        -m qtmultimedia qthttpserver qtwebsockets qtgrpc \
        --outputdir /opt/Qt \
    && rm -rf /opt/aqt-venv \
    && ln -s "/opt/Qt/$QT_VERSION/$QT_DIR" /opt/qt

ENV CMAKE_PREFIX_PATH=/opt/qt
ENV PATH="/opt/qt/bin:${PATH}"
ENV LD_LIBRARY_PATH=/opt/qt/lib

WORKDIR /src
COPY . .

# Same flags as linux.yml (libtorrent static, Boost from apt); deploy-qt bundles Qt libs + plugins into the tarball
RUN test -f external/enodemeta/README.md -a -n "$(ls -A external/specialdlp)" \
        || { echo "submodules missing: git submodule update --init" >&2; exit 1; } \
    && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEMULE_BITTORRENT=ON \
        -DEMULE_BUILD_TESTS=OFF -DEMULE_SEED_FROM_SOURCE_TREE=OFF \
    && cmake --build build --parallel "$(nproc)" \
    && for so in $(find /opt/qt/plugins/ -name '*.so'); do \
        # linuxdeploy aborts on a plugin whose libs are absent (gstreamer, gtk3, ...)
        if ldd "$so" | grep -q "not found"; then \
            echo "pruning Qt plugin with missing deps: $so"; rm -f "$so"; \
        fi; \
    done \
    && scripts/bundle-linux.sh deploy-qt build \
    && mkdir -p /out \
    && tar xzf build/eMuleQt-v*-linux-*.tar.gz -C /out

# === Stage 2: Runtime =======================================================
FROM debian:trixie-slim

ARG VERSION=dev
ENV DEBIAN_FRONTEND=noninteractive

LABEL org.opencontainers.image.title="eMuleQt" \
      org.opencontainers.image.description="eMuleQt core (emulecored) with web UI, REST API and optional GUI via noVNC" \
      org.opencontainers.image.source="https://github.com/ModderMule/emule-qt" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.licenses="GPL-2.0"

# Base libs the bundle leaves to the system, plus the virtual-display GUI stack
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates tini procps \
        libgl1 libegl1 libopengl0 libglib2.0-0t64 libdbus-1-3 libgssapi-krb5-2 \
        libfontconfig1 libfreetype6 fonts-dejavu-core \
        libx11-6 libx11-xcb1 libxext6 libxrender1 libxi6 libxfixes3 libxrandr2 libsm6 libice6 \
        libxkbcommon0 libxkbcommon-x11-0 \
        libxcb1 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
        libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-shm0 libxcb-sync1 \
        libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 libxcb-render0 \
        xvfb x11vnc openbox novnc python3-websockify \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /out/eMuleQt /opt/emuleqt

# Fail the build on an unresolved lib instead of at container start
RUN cd /opt/emuleqt \
    && missing=$(LD_LIBRARY_PATH=/opt/emuleqt/lib ldd emulecored.bin emuleqt.bin emuleqt-mcp.bin \
            plugins/platforms/libqxcb.so plugins/tls/libqopensslbackend.so 2>&1 | grep "not found" || true) \
    && if [ -n "$missing" ]; then echo "$missing" >&2; exit 1; fi

# On PATH, not symlinked: the bundle launchers find lib/ from their own dir
ENV PATH="/opt/emuleqt:${PATH}"

# Unprivileged runtime user; the entrypoint remaps it to PUID/PGID
RUN groupadd -g 1000 emule && useradd -u 1000 -g emule -m -d /home/emule -s /bin/sh emule \
    && mkdir -p /config /data && chown emule:emule /config /data \
    && mkdir -p -m 1777 /tmp/.X11-unix \
    && ln -sf vnc.html /usr/share/novnc/index.html

COPY docker/emuleqt/entrypoint.sh /usr/local/bin/emuleqt-entrypoint
COPY docker/emuleqt/run.sh /usr/local/bin/emuleqt-run
RUN chmod +x /usr/local/bin/emuleqt-entrypoint /usr/local/bin/emuleqt-run

ENV LANG=C.UTF-8 \
    QT_LOGGING_RULES="qt.multimedia.symbolsresolver=false" \
    PUID=1000 PGID=1000 \
    EMULEQT_TCP_PORT=4662 EMULEQT_UDP_PORT=4672 \
    EMULEQT_GUI=0

VOLUME ["/config", "/data"]

# eD2K TCP, eD2K/Kad UDP, web UI/REST/MCP, GUI IPC, noVNC, raw VNC
EXPOSE 4662/tcp 4672/udp 4711/tcp 4712/tcp 5800/tcp 5900/tcp

# IPC port 4712 (0x1268) in LISTEN; a connect probe would log as an IPC client
HEALTHCHECK --interval=30s --timeout=5s --start-period=60s \
    CMD grep -qE ':1268 [0-9A-F]+:0000 0A' /proc/net/tcp /proc/net/tcp6 || exit 1

# The daemon saves part files on SIGTERM; give it time (docker stop -t 60)
STOPSIGNAL SIGTERM

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/emuleqt-entrypoint"]

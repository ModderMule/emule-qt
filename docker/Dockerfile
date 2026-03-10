# ---------------------------------------------------------------------------
# Dockerfile — build eMuleQt for Linux x86_64
#
# Usage (from project root):
#   docker/build.sh
#   docker/build.sh --deploy-qt
#
# DEPLOY_QT=true bundles Qt libraries into the tarball via linuxdeploy
# so the result is fully self-contained (no system Qt required).
#
# Produces: emuleqt-v0.1.1-linux-x86_64.tar.gz in the current directory
# ---------------------------------------------------------------------------

# === Stage 1: Build =========================================================
FROM --platform=linux/amd64 debian:trixie AS builder

ARG DEPLOY_QT=false

ENV DEBIAN_FRONTEND=noninteractive

# System build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        curl \
        file \
        git \
        squashfs-tools \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
        zlib1g-dev \
        libssl-dev \
        # Qt runtime / linking deps
        libgl1-mesa-dev \
        libxkbcommon-dev \
        libxkbcommon-x11-dev \
        libxcb-cursor-dev \
        libxcb-icccm4-dev \
        libxcb-image0-dev \
        libxcb-keysyms1-dev \
        libxcb-render-util0-dev \
        libxcb-shape0-dev \
        libxcb-xinerama0-dev \
        libxcb-xkb-dev \
        libxcb-randr0-dev \
        libxcb1-dev \
        libvulkan-dev \
        libfontconfig1-dev \
        libfreetype6-dev \
        libwayland-dev \
        libegl1-mesa-dev \
        libglib2.0-dev \
        libdbus-1-dev \
        libpulse-dev \
        # X11 runtime libs needed by Qt plugins (linuxdeploy bundling)
        libxrandr-dev \
        libxrender-dev \
        libxext-dev \
        libx11-dev \
        libxfixes-dev \
        libxi-dev \
        libxcursor-dev \
        libxcomposite-dev \
        libxdamage-dev \
        libxtst-dev \
        libxss-dev \
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

# Copy source and build
WORKDIR /src
COPY . .

RUN scripts/build.sh \
    && if [ "$DEPLOY_QT" = "true" ]; then \
        scripts/bundle-linux.sh deploy-qt; \
    else \
        scripts/bundle-linux.sh; \
    fi

# === Stage 2: Export tarball =================================================
FROM scratch
COPY --from=builder /src/emuleqt-v*-linux-x86_64.tar.gz /
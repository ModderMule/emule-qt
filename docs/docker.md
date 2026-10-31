# Docker image

The official image `ghcr.io/moddermule/emuleqt` runs the headless core
(`emulecored`) and optionally the Qt GUI on a virtual display, viewed in a
browser. It is built for `linux/amd64` and `linux/arm64` on every release.

| Tag | Meaning |
|---|---|
| `latest`, `X.Y.Z`, `X.Y` | release builds |
| `edge` | manual build from `main` |

## Quick start

```bash
docker run -d --name emuleqt \
  --stop-timeout 60 \
  -e EMULEQT_WEB_PASSWORD=change-me \
  -p 4662:4662/tcp -p 4662:4662/udp \
  -p 4711:4711 -p 4712:4712 \
  -v ./config:/config -v ./data:/data \
  ghcr.io/moddermule/emuleqt:latest
```

The web UI is at `http://<host>:4711`. A compose example is in
[`docker/emuleqt/compose.yml`](../docker/emuleqt/compose.yml).

Give the container time to stop (`--stop-timeout 60` or `stop_grace_period:
60s`). On `SIGTERM` the core saves its part files before it exits.

## Volumes

| Path | Content |
|---|---|
| `/config` | `preferences.yml`, `server.met`, `nodes.dat`, part-file index, logs. `/config/gui` holds the in-container GUI's own prefs. `ed3k/`: the ed3k node's state (identity key, lists; the hoster resolver and Java runtime only while hoster downloads are on). `BT/`: torrent session and resume data. `webserver/`: only overrides of the web UI files shipped in the image (see its `README.txt`) |
| `/data/incoming` | completed downloads (shared) |
| `/data/temp` | part files |

Files are written as `PUID:PGID` (default `1000:1000`). Set both to your host
user so the bind mounts stay yours.

## Ports

| Port | Use | Expose |
|---|---|---|
| 4662/tcp | eD2K client connections | yes, same port on host and container |
| 4662/udp | eD2K + Kad UDP (and ed3k) | yes, same port on host and container |
| 4711/tcp | web UI, REST API, MCP | LAN or reverse proxy |
| 4712/tcp | GUI IPC (token, TLS) | LAN only |
| 5800/tcp | GUI via noVNC (`EMULEQT_GUI=1`) | LAN only |
| 5900/tcp | raw VNC (`EMULEQT_VNC_EXPOSE=1`) | optional |

Peers are told the P2P ports, so the host side must match. To use other ports,
change both sides together: `-e EMULEQT_TCP_PORT=5662 -p 5662:5662/tcp -p 5662:5662/udp`.
TCP and UDP use one port number (eD2K compat, ed3k Protocol.md §14.1). Forward
both on your router for a HighID. UPnP is off in the container.

Containers created before this keep their `preferences.yml` and with it the old
UDP port (4672): keep mapping `4672:4672/udp` for them.

`--network host` works too. It avoids the Docker NAT and gives the core the
host's IPv6 addresses. Then the `-p` flags are not needed.

## Environment

**These are applied on the first start only**, when `/config/preferences.yml`
does not exist yet. After that the core owns the file and rewrites it. Change
settings through the web UI or the GUI, or stop the container and edit the
file.

| Variable | Default | |
|---|---|---|
| `PUID` / `PGID` | `1000` | owner of `/config` and `/data` (applied every start) |
| `EMULEQT_TCP_PORT` | `4662` | eD2K TCP port |
| `EMULEQT_UDP_PORT` | = TCP port | eD2K/Kad UDP port; one port for TCP and UDP unless set |
| `EMULEQT_NICK` | core default | user nick |
| `EMULEQT_IPC_TOKEN` | random | GUI IPC token; a generated one is printed in `docker logs` |
| `EMULEQT_WEB` | `1` | web server on :4711 |
| `EMULEQT_WEB_PASSWORD` | – | web UI admin password; without it login is refused |
| `EMULEQT_REST` | `0` | REST API under `/api/v1` |
| `EMULEQT_MCP` | `0` | MCP endpoint for AI clients |
| `EMULEQT_API_KEY` | – | key for REST/MCP (`X-Api-Key` or `Authorization: Bearer`) |
| `EMULEQT_GUI` | `0` | run the GUI on a virtual display (applied every start) |
| `EMULEQT_GUI_RES` | `1600x1000x24` | virtual display size |
| `EMULEQT_VNC_PASSWORD` | – | password for noVNC/VNC; without it the GUI is open |
| `EMULEQT_VNC_EXPOSE` | `0` | also listen for raw VNC on 5900 |

## GUI in the browser

```bash
docker run ... -e EMULEQT_GUI=1 -e EMULEQT_VNC_PASSWORD=change-me -p 5800:5800 ...
```

Open `http://<host>:5800`. The image runs Xvfb, openbox, x11vnc and noVNC.
Closing the GUI window restarts the GUI; the core keeps running.

## Native GUI connecting to the container

A GUI on another machine connects over IPC on 4712 with the token. Either
start the GUI without a local core and enter host, port and token in the
connect dialog, or set it in the GUI's `preferences.yml`:

```yaml
ipc:
  enabled: true
  port: 4712
  listenAddress: "192.168.1.10"   # Docker host; 127.0.0.1 if on the same machine
  daemonPath: "local"
  tokens:
    - "<token from docker logs or EMULEQT_IPC_TOKEN>"
```

The link runs over TLS whenever a token is sent. The core creates a
self-signed certificate on first start (`/config/ipc-cert.pem`, `ipc-key.pem`)
and logs its SHA-256 fingerprint. The GUI pins that fingerprint on its first
connect (Options → General → Core → Trusted cores). If the core later shows a
different certificate, the GUI asks before it sends anything. Compare with the
fingerprint in `docker logs` before you trust it. Keep `/config` on a volume, or
every new container gets a new certificate.

Quitting that GUI leaves the core in the container running.

## REST API and MCP

With `EMULEQT_REST=1` and `EMULEQT_API_KEY` set:

```bash
curl -H "X-Api-Key: $KEY" http://<host>:4711/api/v1/openapi.json
```

Swagger UI is at `/api/v1/docs`. See [rest-api.md](rest-api.md). The MCP
bridge for stdio AI clients is in the image and in every release:

```bash
emuleqt-mcp --url http://<host>:4711/mcp --api-key "$KEY"
```

## Security

- 4711, 4712 and 5800 control the client. Keep them on the LAN or behind a
  reverse proxy with TLS. Never forward them on the router.
- 4712 needs TLS from any client that isn't on loopback, and the token is only
  ever sent inside TLS. A plaintext remote client is dropped.
- For HTTPS on the web server, mount a cert and set `webserver.httpsEnabled`,
  `certPath` and `keyPath` in `/config/preferences.yml`.
- Always set `EMULEQT_VNC_PASSWORD` with `EMULEQT_GUI=1`.

## Building locally

From the project root, with submodules checked out
(`git submodule update --init external/enodemeta external/specialdlp`):

```bash
docker build -f docker/emuleqt/Dockerfile -t emuleqt .
```

The build runs `scripts/bundle-linux.sh deploy-qt`, so the image holds the same
layout as the Linux tarball under `/opt/emuleqt`. CI:
[`.github/workflows/docker.yml`](../.github/workflows/docker.yml), called from
`release.yml`. Run it by hand from `main` to push `edge`.

The images under `docker/kad` and `docker/httpcache` are test rigs, not this
image.

# Port mapping: PCP, NAT-PMP and UPnP IGD

eMuleQt opens its listening ports on the router automatically. Three protocols
are supported; the daemon races them at startup and uses whichever the router
actually speaks.

| Protocol | RFC | Families | Notes |
|---|---|---|---|
| **PCP** | 6887 | IPv4 + IPv6 | Preferred. Reports the assigned port and address explicitly, and carries an epoch that reveals a router reboot. |
| **NAT-PMP** | 6886 | IPv4 only | Its only address field is a 32-bit IPv4. Common on OpenWrt/miniupnpd and older Apple gear. |
| **UPnP IGD** | — | IPv4 (IGD1) + IPv6 pinholes (IGD2) | Fallback. IPv6 needs `WANIPv6FirewallControl` with `InboundPinholeAllowed`. |

## How the winner is chosen

All enabled backends probe concurrently, bounded by a 3 s deadline. Candidates
are then tried in order and graded on what they actually achieved:

1. **Exact port match wins, ahead of protocol preference.** eD2K advertises
   `thePrefs.port()` and has no external-port tag, so a grant on a *different*
   external port is a silent LowID. A UPnP backend that honours the port beats a
   PCP backend that does not.
2. Otherwise the `PortMapMethod` ordinal decides: PCP > NAT-PMP > UPnP.

The winner is remembered in `portMapMethod` and tried first next run. Losing
backends' mappings are released so no duplicates are left on the router.

## Status values

`Mapped` means forwarded *and* reachable. `Degraded` means the router granted the
mapping but it will not work from the Internet — either the external port differs
from the internal one, or the external address is not publicly routable (CGNAT,
`100.64.0.0/10`, is the common case). Reporting that as success would show
"forwarded" beside a permanently firewalled client.

## Ports that are mapped

`CoreSession::buildPortMapRequests()` reads the ports the sockets are **actually
bound to**, not the preference values — with a configured port of 0 the OS
assigns one, and forwarding the pref would open a port nothing listens on.

- eD2K TCP listen port
- eD2K/Kad client UDP port
- Web server port, only when `webServerEnabled` and `webServerUPnP` are both set
- IPv6 pinholes for the same ports when `portMapIPv6` is on and we hold a
  confident public IPv6

The **server UDP port is deliberately not mapped**. `UDPSocket` only ever
receives `OP_GLOBSEARCHRES`, `OP_GLOBFOUNDSOURCES`, `OP_GLOBSERVSTATRES` and
`OP_SERVER_DESC_RES` — all replies to requests we sent first, so our own outbound
datagram already opens the NAT binding. An inbound mapping would add attack
surface for nothing.

## Preferences

Under the `upnp:` YAML section (the key names are kept for compatibility):

| Key | Default | Meaning |
|---|---|---|
| `enableUPnP` | true | Master switch for automatic port forwarding |
| `closeUPnPOnExit` | true | Release mappings on clean shutdown |
| `portMapProtocols` | 7 | Bitmask: 1 = PCP, 2 = NAT-PMP, 4 = UPnP |
| `portMapLeaseSecs` | 3600 | Lease requested; the router may grant less |
| `portMapIPv6` | true | Also open IPv6 firewall pinholes |
| `portMapMethod` | 0 | Learned: last winning protocol |
| `portMapSecret` | — | Learned: per-install secret PCP nonces derive from |

`skipWANIPSetup` and `skipWANPPPSetup` were removed — miniupnpc selects the
WANIPConnection/WANPPPConnection service itself, so nothing could honour them.

## Architecture

```
CoreSession ──owns──> PortMapper ──> PortMapBackend
                                      ├─ PcpBackend     ┐ UdpMappingBackend
                                      ├─ NatPmpBackend  ┘ (UDP 5351)
                                      └─ UPnPBackend ──> UPnPWorker (QThread)
net/DefaultGateway   next-hop lookup for the two UDP protocols
```

`PortMapper` never touches `ListenSocket`/`ClientUDPSocket`/`WebServer`:
`CoreSession` hands it a desired-state vector, which is what lets
`tst_PortMapper` drive the whole state machine against a fake backend with no
network.

Renewal is one `QTimer` per mapping, sized from the lifetime the router actually
granted. Nothing port-mapping-related runs on the `CoreSession` tick — the
previous implementation called synchronous SOAP from the 100 ms tick every 30
seconds, so an unreachable router stalled the whole event loop.

## Firmware quirks worth knowing

Measured against a FRITZ!Box with `scripts/probe-portmap.py`. None of these are in
the RFCs, and each one is silent rather than an error.

1. **One PCP request per source port, over IPv6.** A second request from the same
   port is ignored, whatever it is. `UdpMappingBackend` therefore uses a fresh
   socket per *attempt*; reusing one makes the first request work and everything
   after it time out, which looks exactly like "no IPv6 support".
2. **PCP over IPv6 must be sourced from a GUA.** The v6 default route is via a
   link-local next hop, so the kernel picks a link-local source and the router
   answers `NOT_AUTHORIZED` — even to ANNOUNCE, which needs no authorization.
   The socket is bound to the GUA before connecting.
3. **A mapping is owned by its nonce, and a wrong nonce gets silence** rather than
   the `NOT_AUTHORIZED` RFC 6887 §11.3 specifies. Nonces are therefore derived
   from the persisted `portMapSecret`, not generated per process — otherwise a
   crash would leave us unable to renew *or* delete our own mappings for a full
   lease.
4. **PCP delete is never acknowledged.** Delete is fire-and-forget; the finite
   lease is the real cleanup.
5. **NAT-PMP rejection can be 2 bytes**, where RFC 6886 §3.5 mandates 8. A decoder
   that requires 4 bytes never detects the router at all.
6. **The router's `<prefix>::1` is not reachable** — use the link-local next hop
   from the route table, never a synthesized address.

## Testing

```sh
scripts/probe-portmap.py            # what does this network actually support?
scripts/probe-portmap.py --no-map   # read-only: creates nothing on the router
ctest -LE live                      # offline: codecs, gateway parsing, facade
ctest -L live -R tst_PortMapLive    # end-to-end against the real router
```

`tst_PortMapWire` carries golden byte vectors, several captured from a real
FRITZ!Box, including the 2-byte rejection and a successful IPv6 pinhole response.

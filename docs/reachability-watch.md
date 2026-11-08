# Reachability watch (#156)

Port opens after start (VPN / ISP enables a fresh mapping 0–30 min late) → login got LowID, Kad
firewalled, nothing rechecked. Not MFC.

Pref `reachabilityWatch` (default **off**), Options → Server → Miscellaneous:
"Recheck a Low ID when the port opens later". Off = none of the below runs.

## Steps
While eD2K LowID or Kad TCP firewalled (not Kad LAN mode): 2, 10, 30, 60 min after that started,
then stop (normal hourly Kad check stays).
- Kad firewall recheck if Kad firewalled
- website eD2K port test if LowID and we expect to be reachable: port mapped by UPnP/PCP/NAT-PMP,
  or HighID / Kad open in the last 7 days (`lastHighIdAt`). Keeps always-firewalled users off the site.

Restart on network / interface change and on a changed mapped TCP port.

## Evidence → action
| evidence | where |
|---|---|
| Kad TCP open | `Kademlia::firewallStatusChanged(false)` |
| Kad UDP check passed | `UDPFirewallTester::isOpenAndVerified()` turns true |
| direct inbound TCP from public IPv4 | `ClientReqSocket::checkHelloFirst` → `AppContext::noteInboundIPv4Peer` (NAT-T links and the login test excluded) |
| port test TCP open | `PortTestApi::tcpVerdict` |

Action: LowID → `ServerConnect::requestHighIdRecheck`; Kad firewalled → recheck (≥ 120 s apart).

## HighID recheck (ServerConnect)
- server sets TCP flag `0x800000` (eNode-go after v0.5.20): send `OP_RECHECKFIREWALL 0x27 <tcpPort u16>`
  on the live session; server re-runs its dial-back and answers with a fresh `OP_IDCHANGE`
  (HighID or same LowID). ≥ 2 min apart; answer timeout 30 s.
- other servers: background re-login to the same address (handover machinery, reason
  `HighIdRecheck`), live session kept until the new one logs in. ≥ 10 min apart.
- each recheck that keeps a LowID doubles the gap, up to 4×.
- mid-session `OP_IDCHANGE` on the live socket: no smart-LowID bounce, no IPv6 move.
- handover login and pending opcode count for `awaitingTestFromIP` (crypt-required lets the
  server's plain dial-back in) and relax the accept limit (`ListenSocket::serverIsConnecting`).
- mapped port changed: recheck even with a HighID (server learns the new port).

## Server extension (eNode-go)
`ed2k/firewallrecheck.go`. Config `tcp.firewallRecheck` (default true),
`tcp.firewallRecheckInterval` (120 s). Throttled / in flight → no reply. Promotion frees the LowID,
drops its NAT-T state, rewrites client row + source stamps (`Storage.UpdateClient`). HighID never
demoted. Spec: eNode-go `docs/server-client-communication.md`.

## Website port test (`proto=ed2k`)
`PortTestRunner` network 3 (IPC TestPorts too; Test Ports buttons use it, browser page as fallback).
Sends `hash` = user hash when obfuscation is supported → plugin ≥ 1.6.0 dials obfuscated
(RC4 client handshake), so a core that requires encryption answers. Without `hash`: plain.
Plain dial refused by require-crypt (`bad_reply`/`rejected`) = unknown, not closed.

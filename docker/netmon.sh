#!/bin/sh
# netmon.sh — optional network monitor for the Docker rigs.
#
# Built into every daemon image, started only when a container runs with
# NETMON=1 (see the rig entrypoints). Writes two text files to $NETMON_DIR:
#   syn.txt      every TCP SYN/FIN/RST on the daemon port, timestamped
#   samples.txt  one line per interval: socket states, accept queue,
#                kernel TCP counters, load, CPU pressure, daemon CPU ticks
#
# Timestamps are wall-clock, so both files line up with the daemon log.

NETMON_DIR="${NETMON_DIR:-/netmon}"
NETMON_INTERVAL="${NETMON_INTERVAL:-2}"
NETMON_IFACE="${NETMON_IFACE:-eth0}"
TCP_PORT="${TCP_PORT:-5662}"

mkdir -p "$NETMON_DIR"

# Connection setup/teardown only — payload packets would swamp the capture
tcpdump -l -n -tttt -Z root -i "$NETMON_IFACE" \
    "tcp port ${TCP_PORT} and (tcp[tcpflags] & (tcp-syn|tcp-fin|tcp-rst) != 0)" \
    > "$NETMON_DIR/syn.txt" 2> "$NETMON_DIR/tcpdump.err" &

COUNTERS='TcpActiveOpens|TcpPassiveOpens|TcpAttemptFails|TcpRetransSegs|TcpExtTCPSynRetrans|TcpExtListenOverflows|TcpExtListenDrops|TcpExtTCPTimeouts|TcpExtTCPReqQFullDrop'

while :; do
    now="$(date +%H:%M:%S.%3N)"
    states="$(ss -tanH 2>/dev/null | awk '{ n[$1]++ } END { for (s in n) printf "%s%s:%d", (c++ ? "," : ""), s, n[s] }')"
    # Recv-Q/Send-Q of the listener = pending accepts / backlog
    listenq="$(ss -ltnH "sport = :${TCP_PORT}" 2>/dev/null | awk 'NR == 1 { printf "%s/%s", $2, $3 }')"
    counters="$(nstat -az 2>/dev/null | awk -v re="^(${COUNTERS})\$" '$1 ~ re { printf "%s%s:%s", (c++ ? "," : ""), $1, $2 }')"
    load="$(cut -d' ' -f1-4 /proc/loadavg | tr ' ' ',')"
    psi="$(awk '/^some/ { print $2 "," $5 }' /proc/pressure/cpu 2>/dev/null)"
    pid="$(pidof emulecored 2>/dev/null | awk '{ print $1 }')"
    # utime,stime in clock ticks
    cpu="$(awk '{ print $14 "," $15 }' "/proc/${pid:-1}/stat" 2>/dev/null)"
    echo "T=${now} states=${states} listenq=${listenq} counters=${counters} load=${load} psi=${psi} cpu=${cpu}" \
        >> "$NETMON_DIR/samples.txt"
    sleep "$NETMON_INTERVAL"
done

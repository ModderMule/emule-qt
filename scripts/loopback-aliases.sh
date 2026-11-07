#!/usr/bin/env bash
# loopback-aliases.sh — extra 127.0.<row>.<n> addresses for tests with many simulated peers
# (tst_UploadRateStability: one IP per peer, the upload queue allows 3 clients per IP).
#
#   loopback-aliases.sh up|down|status [rows] [first] [last]
#   defaults: rows "1 2", hosts 10..250  ->  127.0.1.10 ... 127.0.2.250
#
# macOS: lo0 has only 127.0.0.1, aliases are added with ifconfig (gone after reboot).
# Linux: 127.0.0.0/8 is local already; addresses are only added if that route is missing.
# Needs sudo for up/down.
set -euo pipefail

cmd=${1:-status}
rows=${2:-"1 2"}
first=${3:-10}
last=${4:-250}

os=$(uname -s)

# runs "$@" ADDR for every address, counts successes in $ok
ok=0; total=0
each() {
    for r in $rows; do
        for i in $(seq "$first" "$last"); do
            total=$((total + 1))
            if "$@" "127.0.$r.$i" 2>/dev/null; then ok=$((ok + 1)); fi
        done
    done
}
report() { echo "$1: $ok of $total addresses (127.0.{${rows// /,}}.$first-$last)"; }

linux_whole_range() {
    # loopback route covers all of 127/8 by default
    ip route show table local 2>/dev/null | grep -q '^local 127\.0\.0\.0/8 dev lo'
}

linux_addr() { sudo ip addr "$1" "$2/32" dev lo; }

case "$os:$cmd" in
    Darwin:up)     sudo -v; each sudo ifconfig lo0 alias; report "added" ;;
    Darwin:down)   sudo -v; each sudo ifconfig lo0 -alias; report "removed" ;;
    Darwin:status) n=$(ifconfig lo0 | grep -c 'inet 127\.0\.[1-9]') || true
                   echo "lo0: $n extra 127.0.x.y aliases" ;;
    Linux:up)      if linux_whole_range; then echo "127.0.0.0/8 already local, nothing to do"
                   else sudo -v; each linux_addr add; report "added"; fi ;;
    Linux:down)    sudo -v; each linux_addr del; report "removed" ;;
    Linux:status)  if linux_whole_range; then echo "127.0.0.0/8 local: all addresses usable"
                   else n=$(ip -4 addr show dev lo | grep -c 'inet 127\.0\.[1-9]') || true
                        echo "lo: $n extra 127.0.x.y addresses"; fi ;;
    *) echo "usage: $0 up|down|status [rows] [first] [last]  (macOS, Linux)" >&2; exit 1 ;;
esac

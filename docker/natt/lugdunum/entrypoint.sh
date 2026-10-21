#!/bin/sh
# eserver reads ./donkey.ini and keeps a console on stdin. A FIFO keeps stdin open and
# takes console commands: docker exec natnet-server sh -c 'echo vc > /tmp/console'
set -e
cd /eserver
cp /rig/donkey.ini donkey.ini
rm -f /tmp/console
mkfifo /tmp/console
sleep infinity > /tmp/console &
ulimit -n 65536 2>/dev/null || true
exec qemu-i386-static ./eserver < /tmp/console

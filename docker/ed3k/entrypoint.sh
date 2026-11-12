#!/bin/sh
# Rig node: route via the NAT router if asked, then run packmule-node with the given args.
set -eu
if [ -n "${RIG_DEFAULT_ROUTE:-}" ]; then
    ip route replace default via "$RIG_DEFAULT_ROUTE"
fi
exec /opt/packmule-node "$@"

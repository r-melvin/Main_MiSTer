#!/bin/bash

# create simple text file named 'host' in this folder with IP address of your MiSTer.

HOST=192.168.1.75
BUILDDIR=bin
[ -f host ] && HOST=$(cat host)

# make script fail if any command failed,
# so we don't need to check the exit status of every command.
set -e
set -o pipefail

echo "Start building..."
make

# Deploy via SCP with SSH key (SFTP with encryption)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
KEY="$SCRIPT_DIR/mister_deploy_key"

set +e
# Kill existing process
ssh -i "$KEY" -o ConnectTimeout=5 root@$HOST 'killall MiSTer' 2>/dev/null || true

set -e
# Deploy binary via encrypted SCP
scp -i "$KEY" "$BUILDDIR/MiSTer" root@$HOST:/media/fat/MiSTer

# Restart
ssh -i "$KEY" root@$HOST 'sync; PATH=/media/fat:$PATH; MiSTer >/dev/ttyS0 2>/dev/ttyS0 </dev/null &'

echo "If SSH key doesn't exist, generate it:"
echo "ssh-keygen -t ed25519 -f mister_deploy_key"
echo "ssh-copy-id -i mister_deploy_key root@$HOST"

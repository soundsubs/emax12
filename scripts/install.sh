#!/bin/sh
# scripts/install.sh -- deploy the built emax12 binary to your Move.
#
# Assumes:
#   - scripts/build.sh has already produced build-arm64/emax12
#   - SSH is already set up per the Schwung manual (the Schwung
#     installer handles this; it drops a key you can reuse here)
#   - DEVICE_HOST defaults to move.local, override with an env var
#
# Module install path follows the confirmed schwung convention (verified
# against real published modules -- schwung-jv880, schwung-rex):
#   /data/UserData/schwung/modules/<category>/<module-name>/
# This module is an audio_fx (it processes incoming audio; it doesn't
# generate its own notes), so it goes under audio_fx/ rather than
# sound_generators/ (where e.g. schwung-jv880 and schwung-rex live).
#
# NOTE ON MANIFESTS: real published modules (schwung-jv880, schwung-rex)
# ship without a manifest.json -- they're just a binary (plus any asset
# subdirectories the module manages itself, like schwung-rex's loops/).
# A manifest/release.json is only needed if you want this listed in the
# public Module Store catalog (via a GitHub release + PR to
# module-catalog.json in the main schwung repo) -- not for a personal
# install via SSH or the "install from GitHub URL" option in Schwung
# Manager (move.local:7700). If this module doesn't show up as expected
# after installing, compare its directory contents against a real
# installed module's directory on your Move (SFTP via Cyberduck, or
# `ssh ableton@move.local ls -la /data/UserData/schwung/modules/audio_fx/<some-existing-module>/`).

set -e

DEVICE_HOST="${DEVICE_HOST:-move.local}"
MODULE_NAME=emax12
MODULE_CATEGORY=audio_fx
REMOTE_DIR="/data/UserData/schwung/modules/${MODULE_CATEGORY}/${MODULE_NAME}"
BIN_PATH="build-arm64/${MODULE_NAME}"

if [ ! -f "$BIN_PATH" ]; then
    echo "error: $BIN_PATH not found -- run scripts/build.sh first" >&2
    exit 1
fi

echo "==> Deploying to ${DEVICE_HOST}:${REMOTE_DIR}"
ssh "ableton@${DEVICE_HOST}" "mkdir -p '${REMOTE_DIR}'"
scp "$BIN_PATH" "ableton@${DEVICE_HOST}:${REMOTE_DIR}/${MODULE_NAME}"
ssh "ableton@${DEVICE_HOST}" "chmod +x '${REMOTE_DIR}/${MODULE_NAME}'"

echo "==> Done. If the module doesn't appear in Shadow UI, restart"
echo "    Schwung Manager (or reboot Move) so it picks up the new"
echo "    module directory, then check move.local:7700/modules."
echo "==> To test manually over SSH first (bypassing Shadow UI):"
echo "    ssh ableton@${DEVICE_HOST} '${REMOTE_DIR}/${MODULE_NAME}'"

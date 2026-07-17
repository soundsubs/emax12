#!/bin/sh
# scripts/install.sh -- deploy the built Emax_FX.so + module.json to your
# Move directly over SSH (bypasses the Module Store / Install Custom
# Module UI entirely -- useful for iterating during development).
#
# Assumes:
#   - scripts/build.sh has already produced build-arm64/Emax_FX.so
#   - SSH is already set up per the Schwung manual
#   - DEVICE_HOST defaults to move.local, override with an env var
#
# CORRECTED: earlier versions of this script deployed a bare JACK-client
# binary with no module.json, which is not a valid Schwung module at all
# (confirmed via docs/MODULES.md's "Module Structure" section: module.json
# is REQUIRED). This version deploys both the plugin shared library and
# its manifest, matching the real on-device layout:
#   /data/UserData/schwung/modules/audio_fx/Emax_FX/module.json
#   /data/UserData/schwung/modules/audio_fx/emax12/Emax_FX.so

set -e

DEVICE_HOST="${DEVICE_HOST:-move.local}"
MODULE_NAME=Emax_FX
MODULE_CATEGORY=audio_fx
REMOTE_DIR="/data/UserData/schwung/modules/${MODULE_CATEGORY}/${MODULE_NAME}"
BIN_PATH="build-arm64/${MODULE_NAME}.so"
MANIFEST_PATH="module.json"

if [ ! -f "$BIN_PATH" ]; then
    echo "error: $BIN_PATH not found -- run scripts/build.sh first" >&2
    exit 1
fi
if [ ! -f "$MANIFEST_PATH" ]; then
    echo "error: $MANIFEST_PATH not found at repo root" >&2
    exit 1
fi

echo "==> Deploying to ${DEVICE_HOST}:${REMOTE_DIR}"
ssh "ableton@${DEVICE_HOST}" "mkdir -p '${REMOTE_DIR}'"
scp "$BIN_PATH" "ableton@${DEVICE_HOST}:${REMOTE_DIR}/${MODULE_NAME}.so"
scp "$MANIFEST_PATH" "ableton@${DEVICE_HOST}:${REMOTE_DIR}/module.json"

echo "==> Done. Restart Schwung Manager (or reboot Move) so it picks up"
echo "    the new module directory, then check move.local:7700/modules"
echo "    or add it to a Signal Chain slot's Audio FX."

#!/bin/sh
# scripts/package.sh -- build the release tarball for GitHub Releases.
#
# Schwung Manager's "Install Custom Module" flow (move.local:7700/modules)
# reads release.json from the repo root, then downloads and extracts the
# tarball named at "download_url". Confirmed against a real published
# module (timncox/schwung-mark's release.json:
#   {"version": "0.3.3",
#    "download_url": ".../releases/download/v0.3.3/mark-module.tar.gz"})
#
# Naming convention observed across published modules: <name>-module.tar.gz
# (e.g. mark-module.tar.gz, minijv-module.tar.gz).
#
# Requires: scripts/build.sh has already produced build-arm64/emax12.

set -e

MODULE_NAME=emax12
MODULE_CATEGORY=audio_fx
BIN_PATH="build-arm64/${MODULE_NAME}"
OUT_TAR="${MODULE_NAME}-module.tar.gz"
STAGE_DIR="$(pwd)/.package-stage"

if [ ! -f "$BIN_PATH" ]; then
    echo "error: $BIN_PATH not found -- run scripts/build.sh first" >&2
    exit 1
fi

rm -rf "$STAGE_DIR"
mkdir -p "${STAGE_DIR}/${MODULE_CATEGORY}/${MODULE_NAME}"
cp "$BIN_PATH" "${STAGE_DIR}/${MODULE_CATEGORY}/${MODULE_NAME}/${MODULE_NAME}"
chmod +x "${STAGE_DIR}/${MODULE_CATEGORY}/${MODULE_NAME}/${MODULE_NAME}"

tar -C "$STAGE_DIR" -czf "$OUT_TAR" "${MODULE_CATEGORY}"
rm -rf "$STAGE_DIR"

echo "==> Built: ${OUT_TAR}"
echo "    Next steps:"
echo "    1. Create a GitHub Release (tag matching the version in release.json,"
echo "       e.g. v0.1.0) on your emax12 repo."
echo "    2. Upload ${OUT_TAR} as a release asset."
echo "    3. Make sure release.json's download_url matches the release tag"
echo "       and asset filename exactly."
echo "    4. In Schwung Manager (move.local:7700/modules), use 'Install"
echo "       Custom Module' with your repo URL."

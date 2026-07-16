#!/bin/sh
# scripts/package.sh -- build the release tarball for GitHub Releases /
# Schwung Manager's "Install Custom Module" flow.
#
# CORRECTED STRUCTURE (per docs/MODULES.md "Publishing to Module Store"):
#   <id>-module.tar.gz
#     └── <id>/
#         ├── module.json   <- REQUIRED, this is what installs were
#         |                    missing before ("No module.json found
#         |                    in tarball")
#         └── <id>.so       <- audio_fx plugin shared library
#
# Earlier versions of this script packaged a bare JACK-client binary
# under audio_fx/emax12/emax12 with no module.json at all -- wrong on
# both counts (wrong architecture, wrong tarball shape). This version
# matches the real schema confirmed against docs/MODULES.md and the
# working example release.json from timncox/schwung-mark.
#
# Requires: scripts/build.sh has already produced build-arm64/Emax_FX.so.

set -e

MODULE_NAME=Emax_FX
BIN_PATH="build-arm64/${MODULE_NAME}.so"
MANIFEST_PATH="module.json"
OUT_TAR="${MODULE_NAME}-module.tar.gz"
STAGE_DIR="$(pwd)/.package-stage"

if [ ! -f "$BIN_PATH" ]; then
    echo "error: $BIN_PATH not found -- run scripts/build.sh first" >&2
    exit 1
fi
if [ ! -f "$MANIFEST_PATH" ]; then
    echo "error: $MANIFEST_PATH not found at repo root" >&2
    exit 1
fi

rm -rf "$STAGE_DIR"
mkdir -p "${STAGE_DIR}/${MODULE_NAME}"
cp "$BIN_PATH" "${STAGE_DIR}/${MODULE_NAME}/${MODULE_NAME}.so"
cp "$MANIFEST_PATH" "${STAGE_DIR}/${MODULE_NAME}/module.json"

tar -C "$STAGE_DIR" -czf "$OUT_TAR" "${MODULE_NAME}"
rm -rf "$STAGE_DIR"

echo "==> Built: ${OUT_TAR}"
tar -tzf "$OUT_TAR"
echo "    Next steps:"
echo "    1. Create/update a GitHub Release (tag matching release.json's"
echo "       version, e.g. v0.1.0) on your emax12 repo."
echo "    2. Upload ${OUT_TAR} as a release asset (replacing the old one)."
echo "    3. Confirm release.json's download_url matches exactly."
echo "    4. In Schwung Manager (move.local:7700/modules), use 'Install"
echo "       Custom Module' with your repo URL."

#!/bin/sh
# scripts/build.sh -- cross-compile Emax_FX.so for Move (aarch64 Linux).
#
# CORRECTED ARCHITECTURE: emax12 is a Schwung audio_fx PLUGIN -- a
# shared library (Emax_FX.so) dlopen()'d by Schwung's chain host and
# exporting move_audio_fx_init_v2() (see src/audio_fx_api_v2.h and
# docs/MODULES.md in charlesvestal/schwung). Earlier drafts built a
# standalone JACK2 client instead, which is not how Schwung audio_fx
# modules actually load -- that mismatch is why the Module Store kept
# rejecting the tarball ("No module.json found"). This version builds
# the real plugin shared library and no longer depends on JACK headers.
#
# Requires: Docker.

set -e

IMAGE_NAME=emax12-builder
OUT_DIR="$(pwd)/build-arm64"

mkdir -p "$OUT_DIR"

cat > /tmp/emax12-Dockerfile <<'EOF'
FROM arm64v8/debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential pkg-config \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
EOF

echo "==> Building cross-compile image (arm64v8/debian base, needs QEMU"
echo "    user-mode emulation registered -- run 'docker run --privileged"
echo "    --rm tonistiigi/binfmt --install arm64' once if this image"
echo "    fails to start)."
docker build --platform=linux/arm64 -t "$IMAGE_NAME" -f /tmp/emax12-Dockerfile .

echo "==> Compiling inside container..."
docker run --rm --platform=linux/arm64 \
    -v "$(pwd)":/work \
    -v "$OUT_DIR":/out \
    "$IMAGE_NAME" \
    sh -c "cc -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -fPIC \
           -shared -o /out/Emax_FX.so src/emax_dsp.c src/emax_audio_fx.c -lm"

echo "==> Built: $OUT_DIR/Emax_FX.so"
echo "    Next: scripts/package.sh"

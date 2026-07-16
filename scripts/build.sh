#!/bin/sh
# scripts/build.sh -- cross-compile emax12 for Move (aarch64 Linux).
#
# Move runs an ARM64 (Cortex-A72) SoC, not the architecture you're
# developing on, so the on-device binary needs a real cross-compile.
# This follows the same Docker-based cross-compile pattern schwung-rex
# uses (see its .github/workflows and scripts/build.sh for the reference
# this is modeled on -- I could not fetch that file's exact contents
# directly, GitHub blocked directory access for me here, so double check
# your Docker base image / JACK header source against theirs if this
# doesn't build cleanly).
#
# Requires: Docker.

set -e

IMAGE_NAME=emax12-builder
OUT_DIR="$(pwd)/build-arm64"

mkdir -p "$OUT_DIR"

cat > /tmp/emax12-Dockerfile <<'EOF'
FROM arm64v8/debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential pkg-config libjack-jackd2-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
EOF

echo "==> Building cross-compile image (arm64v8/debian base, needs QEMU"
echo "    user-mode emulation registered -- run 'docker run --privileged"
echo "    --rm tonistiigi/binfmt --install arm64' once if this image"
echo "    fails to start)."
docker build -t "$IMAGE_NAME" -f /tmp/emax12-Dockerfile .

echo "==> Compiling inside container..."
docker run --rm \
    -v "$(pwd)":/work \
    -v "$OUT_DIR":/out \
    "$IMAGE_NAME" \
    sh -c "cc -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L \
           -o /out/emax12 src/emax_dsp.c src/main_jack.c -ljack -lm"

echo "==> Built: $OUT_DIR/emax12"
echo "    Next: scripts/install.sh"

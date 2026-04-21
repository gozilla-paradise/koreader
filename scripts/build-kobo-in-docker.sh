#!/usr/bin/env bash
#
# Build a Kobo release of KOReader inside the official koreader/kokobo
# Docker image (toolchain pre-baked — no gen-tc.sh step needed).
#
# Usage:  scripts/build-kobo-in-docker.sh [kobo|kobov4|kobov5]   (default: kobov5)
#
# The image is linux/amd64-only (x-tools are amd64 binaries). On Apple
# Silicon the container runs under qemu — slower but correct.

set -euo pipefail

VARIANT="${1:-kobov5}"
case "$VARIANT" in
    kobo | kobov4 | kobov5) ;;
    *) echo "Unknown variant: $VARIANT (want kobo|kobov4|kobov5)" >&2; exit 1 ;;
esac

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${KOKOBO_IMAGE:-koreader/kokobo:latest}"

INNER_SCRIPT=$(cat <<'INNER'
set -euo pipefail

: "${VARIANT:?VARIANT env var missing}"

# We're running as root against a host-mounted tree owned by a different
# UID (macOS user). Git's dubious-ownership check would block every
# operation; mark anything safe.
git config --global --add safe.directory '*'

# Install the host trietool (kokobo doesn't ship it). libdatrie's CMake
# wrapper does find_program(HOST_TRIETOOL trietool REQUIRED) for cross
# builds to compile libthai's stock dictionary.
if ! command -v trietool >/dev/null 2>&1; then
    apt-get update -qq
    apt-get install -y --no-install-recommends libdatrie-dev libdatrie1-bin
fi

# Pre-populate fbink to dodge kodev's race on clone_git_repo (multiple
# parallel ninja jobs hit the same source dir; the script's flock on fd 9
# only attaches inside the clone_git_repo function, so the initial
# dir-exists check races).
FBINK_SRC=base/thirdparty/fbink/build/downloads/source
FBINK_SHA=92e127008145b2a22fba7c59815d810d716310dd
if [ ! -d "$FBINK_SRC/.git" ]; then
    rm -rf "$FBINK_SRC"
    mkdir -p "$(dirname "$FBINK_SRC")"
    git clone https://github.com/NiLuJe/FBInk.git "$FBINK_SRC"
    git -C "$FBINK_SRC" checkout "$FBINK_SHA"
    git -C "$FBINK_SRC" submodule update --init --recursive || true
fi

exec ./kodev release "$VARIANT"
INNER
)

docker run --rm --platform linux/amd64 --user 0:0 \
    -v "$REPO_ROOT":/home/ko/koreader \
    -w /home/ko/koreader \
    -e "VARIANT=$VARIANT" \
    "$IMAGE" \
    bash -lc "$INNER_SCRIPT"

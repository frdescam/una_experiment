#!/usr/bin/env bash
# Build the watch apps. Run tools/bootstrap.sh and `source tools/env.sh` first.
#
#   tools/build.sh                RideLock only
#   tools/build.sh --activity     also Run, Bike and Hike with patches/ applied
#
# Results are copied to dist/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${UNA_SDK:?UNA_SDK is not set: run tools/bootstrap.sh, then source tools/env.sh}"
command -v arm-none-eabi-gcc >/dev/null || { echo "arm-none-eabi-gcc not on PATH (source tools/env.sh)" >&2; exit 1; }

mkdir -p "$ROOT/dist"

echo "==> RideLock"
APP="$ROOT/apps/RideLock/Software/Apps/RideLock-CMake"
cmake -G "Unix Makefiles" -S "$APP" -B "$APP/build" >/dev/null
cmake --build "$APP/build" -j"$(nproc 2>/dev/null || echo 4)" 2>&1 | grep -E "Image  |error:" || true
cp "$APP"/build/RideLock_*.uapp "$ROOT/dist/"

if [ "${1:-}" = "--activity" ]; then
    # A separate worktree of the SDK, so the patch never touches $UNA_SDK.
    PATCHED="$ROOT/.deps/una-sdk-patched"
    COMMIT="$(git -C "$UNA_SDK" rev-parse HEAD)"
    if [ ! -d "$PATCHED" ]; then
        git -C "$UNA_SDK" worktree add --quiet --detach "$PATCHED" "$COMMIT"
    fi
    git -C "$PATCHED" checkout --quiet --force "$COMMIT"
    git -C "$PATCHED" clean -fdq Examples/Apps
    git -C "$PATCHED" apply "$ROOT/patches/activity-apps.patch"
    for app in Running Cycling Hiking; do
        echo "==> $app (patched)"
        DIR="$PATCHED/Examples/Apps/$app/Software/Apps/$app-CMake"
        UNA_SDK="$PATCHED" cmake -G "Unix Makefiles" -S "$DIR" -B "$DIR/build" -DBUILD_VERSION=1.5.0-ridelock >/dev/null
        UNA_SDK="$PATCHED" cmake --build "$DIR/build" -j"$(nproc 2>/dev/null || echo 4)" 2>&1 | grep -E "Image  |error:" || true
        cp "$DIR"/build/*_1.5.0-ridelock.uapp "$ROOT/dist/"
    done
fi

( cd "$ROOT/dist" && sha256sum ./*.uapp 2>/dev/null | sed 's# \./# #' > SHA256SUMS || true )
echo "==> dist/"
ls -l "$ROOT/dist"

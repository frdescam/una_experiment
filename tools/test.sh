#!/usr/bin/env bash
# Run the checks that need no watch.
#
#   tools/test.sh            host unit tests (GoogleTest) + jacket model report
#   tools/test.sh --sim      also the PC simulator scripts and the service
#                            harness (needs UNA_SDK, SDL2 dev files, Ninja)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-tests"

echo "==> Host unit tests"
cmake -S "$ROOT/tests" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
ctest --test-dir "$BUILD" --output-on-failure | tail -3

echo "==> Jacket model (200 h per model; docs/jacket-report.md is the 1000 h run)"
"$BUILD/jacket_report" 200 | head -10

if [ "${1:-}" = "--sim" ]; then
    : "${UNA_SDK:?UNA_SDK is not set: run tools/bootstrap.sh, then source tools/env.sh}"
    SIM="$ROOT/apps/RideLock/Software/Apps/LVGL-GUI/simulator"
    echo "==> PC simulator"
    cmake -S "$SIM" -B "$SIM/build-ninja" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
    cmake --build "$SIM/build-ninja" >/dev/null
    SHOTS="$(mktemp -d)"
    ( cd "$SIM/build/bin"
      for script in unlock jacket drain chord; do
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RIDELOCK_SIM_SHOTS="$SHOTS" \
          RIDELOCK_SIM_SCRIPT="../../scripts/$script.txt" ./RideLockSimulator 2>&1 \
              | grep -E "SCRIPT (PASS|FAIL)" | sed "s/^/  $script: /"
      done )
    echo "  screenshots (PPM) in $SHOTS"
    echo "==> Service harness (relock and hidden take about 70 s each)"
    for scenario in noboot manual relock hidden; do
        "$SIM/build/bin/RideLockServiceHarness" "$scenario" "$(mktemp -d)" | grep -E "^HARNESS (PASS|FAIL)" | sed "s/^/  /"
    done
fi

#!/usr/bin/env bash
# Fetch everything needed to build RideLock (and the patched activity apps)
# into .deps/, without touching the rest of the system:
#
#   .deps/una-sdk     UNAWatch/una-sdk at the commit this repo was built
#                     against, with its LVGL submodule
#   .deps/toolchain   Arm GNU Toolchain 13.3.rel1 (arm-none-eabi). The SDK
#                     docs recommend ST's build of GCC from STM32CubeCLT; the
#                     Arm build of the same GCC version builds these apps too
#                     (the ST-only -fcyclomatic-complexity flag is probed and
#                     skipped by the SDK). Set ARM_GCC_BIN to use your own.
#   .deps/venv        Python with the SDK packer's requirements
#
# Then:  source tools/env.sh
#
# Needs: git, curl, tar (xz), python3 with venv, cmake >= 3.21, make.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS="$ROOT/.deps"
SDK_URL="https://github.com/UNAWatch/una-sdk.git"
SDK_COMMIT="b5749dc4e89d1c8d21b1f4dd2f305b2bd43cac98"
TC_VERSION="13.3.rel1"

mkdir -p "$DEPS"

# --- UNA SDK ----------------------------------------------------------------
if [ ! -d "$DEPS/una-sdk/.git" ]; then
    echo "==> Cloning the UNA SDK"
    git clone --quiet "$SDK_URL" "$DEPS/una-sdk"
fi
echo "==> UNA SDK at ${SDK_COMMIT:0:8}"
git -C "$DEPS/una-sdk" fetch --quiet origin "$SDK_COMMIT" 2>/dev/null || true
git -C "$DEPS/una-sdk" checkout --quiet "$SDK_COMMIT"
git -C "$DEPS/una-sdk" submodule update --init --quiet ThirdParty/lvgl

# --- Toolchain --------------------------------------------------------------
if [ -n "${ARM_GCC_BIN:-}" ]; then
    echo "==> Using arm-none-eabi-gcc from ARM_GCC_BIN=$ARM_GCC_BIN"
elif [ ! -x "$DEPS/toolchain/bin/arm-none-eabi-gcc" ]; then
    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64)  HOST="x86_64" ;;
        Linux-aarch64) HOST="aarch64" ;;
        Darwin-arm64)  HOST="darwin-arm64" ;;
        Darwin-x86_64) HOST="darwin-x86_64" ;;
        *) echo "No prebuilt Arm toolchain for $(uname -sm); install one and set ARM_GCC_BIN" >&2; exit 1 ;;
    esac
    NAME="arm-gnu-toolchain-${TC_VERSION}-${HOST}-arm-none-eabi"
    URL="https://developer.arm.com/-/media/Files/downloads/gnu/${TC_VERSION}/binrel/${NAME}.tar.xz"
    echo "==> Downloading $NAME (about 150 MB)"
    TMP="$(mktemp -d)"
    curl -fSL --progress-bar -o "$TMP/tc.tar.xz" "$URL"
    curl -fsSL -o "$TMP/tc.sha256asc" "$URL.sha256asc"
    EXPECTED="$(awk '{print $1}' "$TMP/tc.sha256asc")"
    if [ "$HOST" = "x86_64" ]; then
        # Checked when this repository was made.
        [ "$EXPECTED" = "95c011cee430e64dd6087c75c800f04b9c49832cc1000127a92a97f9c8d83af4" ] || {
            echo "Unexpected published checksum for $NAME" >&2; exit 1; }
    fi
    ACTUAL="$( (sha256sum "$TMP/tc.tar.xz" 2>/dev/null || shasum -a 256 "$TMP/tc.tar.xz") | awk '{print $1}')"
    [ "$ACTUAL" = "$EXPECTED" ] || { echo "Checksum mismatch for $NAME" >&2; exit 1; }
    echo "==> Unpacking"
    tar -xf "$TMP/tc.tar.xz" -C "$TMP"
    rm -rf "$DEPS/toolchain"
    mv "$TMP/$NAME" "$DEPS/toolchain"
    rm -rf "$TMP"
fi

# --- Python -----------------------------------------------------------------
if [ ! -x "$DEPS/venv/bin/python3" ]; then
    echo "==> Python environment for the SDK's packer"
    python3 -m venv "$DEPS/venv"
fi
"$DEPS/venv/bin/python3" -m pip install --quiet --upgrade pip
"$DEPS/venv/bin/python3" -m pip install --quiet -r "$DEPS/una-sdk/Utilities/Scripts/app_packer/requirements.txt"

echo
echo "Done. Now:  source tools/env.sh && tools/build.sh"

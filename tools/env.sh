# Source me:  source tools/env.sh
# Points UNA_SDK at .deps/una-sdk (unless already set) and puts the toolchain
# and the packer's Python first on PATH.
_ridelock_root="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export UNA_SDK="${UNA_SDK:-$_ridelock_root/.deps/una-sdk}"
if [ -n "${ARM_GCC_BIN:-}" ]; then
    export PATH="$ARM_GCC_BIN:$PATH"
elif [ -d "$_ridelock_root/.deps/toolchain/bin" ]; then
    export PATH="$_ridelock_root/.deps/toolchain/bin:$PATH"
fi
if [ -d "$_ridelock_root/.deps/venv/bin" ]; then
    export PATH="$_ridelock_root/.deps/venv/bin:$PATH"
fi
echo "UNA_SDK=$UNA_SDK"
command -v arm-none-eabi-gcc >/dev/null && arm-none-eabi-gcc --version | head -1
unset _ridelock_root

# Source me:  source tools/env.sh
# Points UNA_SDK at .deps/una-sdk (unless already set) and puts the toolchain
# and the packer's Python first on PATH.
_buttonlock_root="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export UNA_SDK="${UNA_SDK:-$_buttonlock_root/.deps/una-sdk}"
if [ -n "${ARM_GCC_BIN:-}" ]; then
    export PATH="$ARM_GCC_BIN:$PATH"
elif [ -d "$_buttonlock_root/.deps/toolchain/bin" ]; then
    export PATH="$_buttonlock_root/.deps/toolchain/bin:$PATH"
fi
if [ -d "$_buttonlock_root/.deps/venv/bin" ]; then
    export PATH="$_buttonlock_root/.deps/venv/bin:$PATH"
fi
echo "UNA_SDK=$UNA_SDK"
command -v arm-none-eabi-gcc >/dev/null && arm-none-eabi-gcc --version | head -1
unset _buttonlock_root

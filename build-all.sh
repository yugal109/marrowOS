#!/usr/bin/env bash
# EDK2 MarrowOS.efi + kernel, then pack bin/uefi.img
# Must be bash (edksetup.sh is not happy under zsh).
# No -u: edksetup.sh tests $PYTHON_COMMAND before it is set.
set -eo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
EDK2="$ROOT/edk2"
EFI_OUT="$EDK2/Build/MdeModule/DEBUG_XCODE5/X64/MarrowOS.efi"

if [[ ! -f "$EDK2/edksetup.sh" ]]; then
  echo "missing $EDK2 (clone/build EDK2 first)"
  exit 1
fi

export PATH="/usr/bin:/opt/homebrew/bin:$HOME/opt64/cross/bin:$PATH"
export PREFIX="${PREFIX:-$HOME/opt64/cross}"
export TARGET="${TARGET:-x86_64-elf}"
export EDK_TOOLS_PATH="$EDK2/BaseTools"
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"

echo "==> EDK2 MarrowOS.efi (XCODE5)"
cd "$EDK2"
# shellcheck disable=SC1091
source ./edksetup.sh
build -a X64 -t XCODE5 -p MdeModulePkg/MdeModulePkg.dsc \
  -m MdeModulePkg/Application/MarrowOS/MarrowOS.inf

mkdir -p "$ROOT/bin"
cp "$EFI_OUT" "$ROOT/bin/MarrowOS.efi"
if [[ ! -f "$ROOT/bin/OVMF_VARS.fd" ]]; then
  cp /opt/homebrew/share/qemu/edk2-i386-vars.fd "$ROOT/bin/OVMF_VARS.fd"
fi

echo "==> creating GPT bin/uefi.img (ESP + MARROW, same idea as Linux+QEMU parted)"
python3 "$ROOT/scripts/make_gpt_disk.py" "$ROOT/bin/uefi.img"
# p1 ESP at 1MiB (LBA 2048), p2 MARROW at 32MiB (LBA 65536)
ESP="$ROOT/bin/uefi.img@@1048576"
MARROW="$ROOT/bin/uefi.img@@33554432"
MTOOLS_SKIP_CHECK=1 mformat -i "$ESP" -v ESP -T 63488 ::
MTOOLS_SKIP_CHECK=1 mformat -i "$MARROW" -v MARROW -T 65503 ::
MTOOLS_SKIP_CHECK=1 mmd -i "$ESP" ::EFI
MTOOLS_SKIP_CHECK=1 mmd -i "$ESP" ::EFI/BOOT
MTOOLS_SKIP_CHECK=1 mcopy -i "$ESP" -o "$ROOT/bin/MarrowOS.efi" ::EFI/BOOT/BOOTX64.EFI
MTOOLS_SKIP_CHECK=1 mcopy -i "$ESP" -o "$ROOT/uefi/startup.nsh" ::startup.nsh

echo "==> kernel"
cd "$ROOT"
make all

echo "done"
MTOOLS_SKIP_CHECK=1 mdir -i "$ROOT/bin/uefi.img@@1048576" ::EFI/BOOT
MTOOLS_SKIP_CHECK=1 mdir -i "$ROOT/bin/uefi.img@@33554432" ::

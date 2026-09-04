#!/usr/bin/env bash
# Mac QEMU + OVMF. Same idea as Linux+QEMU's run.sh (-machine pc, IDE, OVMF).
# Homebrew firmware is pflash, not -bios /usr/share/ovmf/OVMF.fd
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

OVMF_CODE="${OVMF_CODE:-/opt/homebrew/share/qemu/edk2-x86_64-code.fd}"
OVMF_VARS_SRC="${OVMF_VARS_SRC:-/opt/homebrew/share/qemu/edk2-i386-vars.fd}"

if [[ ! -f bin/uefi.img ]]; then
  echo "missing bin/uefi.img — run ./build-all.sh first"
  exit 1
fi

# Fresh NVRAM so OVMF does not keep "boot to UEFI Shell"
cp "$OVMF_VARS_SRC" bin/OVMF_VARS.fd

# pc = PIIX IDE at 0x1F0 (q35 is AHCI; OVMF + kernel ATA both miss the disk)
qemu-system-x86_64 \
  -machine pc \
  -cpu qemu64 \
  -m 512M \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,file=bin/OVMF_VARS.fd \
  -hda bin/uefi.img \
  -display cocoa,zoom-to-fit=on \
  -net none

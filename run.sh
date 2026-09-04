#!/usr/bin/env bash
# Mac QEMU + OVMF. Same idea as Daniel's run.sh (-machine pc, IDE, OVMF).
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

if [[ ! -f bin/OVMF_VARS.fd ]]; then
  cp "$OVMF_VARS_SRC" bin/OVMF_VARS.fd
fi

qemu-system-x86_64 \
  -machine q35 \
  -cpu qemu64 \
  -m 512M \
  -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd \
  -drive if=pflash,format=raw,file=bin/OVMF_VARS.fd \
  -drive file=bin/uefi.img,format=raw,if=ide \
  -display cocoa,zoom-to-fit=on \
  -net none

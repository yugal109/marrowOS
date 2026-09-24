qemu-system-x86_64 \
  -machine q35 \
  -m 512M \
  -cpu Skylake-Server,tsc-frequency=2800000000 \
  -bios /usr/share/ovmf/OVMF.fd \
  -display gtk,zoom-to-fit=on \
  \
  -device pcie-root-port,id=rp1,port=0x10,bus=pcie.0,chassis=1 \
  -device pcie-root-port,id=rp2,port=0x11,bus=pcie.0,chassis=2 \
  \
  -device virtio-net-pci,netdev=n0,bus=rp1,addr=0x0 \
  -netdev user,id=n0 \
  \
  -drive file=./bin/os.img,if=none,id=nvme0,format=raw \
  -device nvme,drive=nvme0,serial=nvme0,bus=rp2,addr=0x0 \
  \
  -device usb-ehci,id=ehci \
  -device usb-kbd,bus=ehci.0,usb_version=2 \
  -device usb-mouse,bus=ehci.0,usb_version=2

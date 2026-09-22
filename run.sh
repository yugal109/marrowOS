qemu-system-x86_64 \
  -machine q35 \
  -m 512M \
  -cpu Skylake-Server,tsc-frequency=2800000000 \
  -bios /usr/share/ovmf/OVMF.fd \
  \
  -device pcie-root-port,id=rp1,port=0x10,bus=pcie.0,chassis=1 \
  -device pcie-root-port,id=rp2,port=0x11,bus=pcie.0,chassis=2 \
  -device pcie-root-port,id=rp3,port=0x12,bus=pcie.0,chassis=3 \
  \
  -device virtio-net-pci,netdev=n0,bus=rp1,addr=0x0 \
  -netdev user,id=n0 \
  \
  -drive file=./bin/os.img,if=none,id=nvme0,format=raw \
  -device nvme,drive=nvme0,serial=nvme0,bus=rp2,addr=0x0 \
  \
  -device qemu-xhci,id=xhci,bus=rp3,addr=0x0 \
  -device usb-kbd,bus=xhci.0 \
  -device usb-mouse,bus=xhci.0

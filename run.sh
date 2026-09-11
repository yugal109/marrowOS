qemu-system-x86_64 \
  -machine pc \
  -drive file=./bin/os.img,format=raw,if=ide \
  -m 8G \
  -cpu qemu64 \
  -bios /usr/share/ovmf/OVMF.fd \
  -usb -device usb-tablet


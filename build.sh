CURRENT_DIR=$(pwd)
set -e

export PREFIX="${PREFIX:-$HOME/opt64/cross}"
export TARGET="${TARGET:-x86_64-elf}"
export PATH="$PREFIX/bin:$PATH"

export EDK_TOOLS_PATH="$CURRENT_DIR/edk2/BaseTools"
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
cd "$CURRENT_DIR/edk2"
. edksetup.sh BaseTools
# Only MarrowOS — full MdeModulePkg needs iasl and builds tons of unused drivers
build -a X64 -t GCC -p MdeModulePkg/MdeModulePkg.dsc \
  -m MdeModulePkg/Application/MarrowOS/MarrowOS.inf

cd "$CURRENT_DIR"

mkdir -p ./bin /mnt/d

dd if=/dev/zero bs=1048576 count=700 of=./bin/os.img
LOOPDEV=$(sudo losetup --find --show --partscan ./bin/os.img)
echo "Loop Device $LOOPDEV"

# Create a GPT partition
sudo parted "$LOOPDEV" --script mklabel gpt

# Create an EFI system parition
sudo parted "$LOOPDEV" --script mkpart ESP fat16 1Mib 50%

# Create another parition
sudo parted "$LOOPDEV" --script mkpart ESP fat16 50% 100%

# Mark the partition as bootable
sudo parted "$LOOPDEV" --script set 1 esp on
sudo partprobe "$LOOPDEV"
sleep 2
lsblk "$LOOPDEV"

sudo mkfs.vfat -n ABC "${LOOPDEV}p1"
# Must match MARROWOS_KERNEL_FILESYSTEM_NAME in src/disk/disk.h ("MARROW     ")
sudo mkfs.vfat -n MARROW "${LOOPDEV}p2"

sudo mount -t vfat "${LOOPDEV}p2" /mnt/d

# Build the 64 bit kernel (this repo root — not nested PeachOS64Bit)
make clean
make all

# Copy the UEFI bootloader from sibling edk2
cp ./edk2/Build/MdeModule/DEBUG_GCC/X64/MarrowOS.efi ./bin/MarrowOS.efi

# Copy the EFI file into partition two
sudo mkdir -p /mnt/d/EFI/BOOT
sudo cp ./bin/MarrowOS.efi /mnt/d/EFI/Boot/BOOTX64.efi
sudo umount /mnt/d
sudo losetup -d "$LOOPDEV"

echo "Build completed"

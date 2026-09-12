#include "disk.h"
#include "io/io.h"
#include "memory/memory.h"
#include "status.h"
#include "config.h"
#include "kernel.h"
#include "memory/heap/kheap.h"
#include "string/string.h"
#include "lib/vector/vector.h"
#include <stdint.h>

#define ATA_PRIMARY_IO 0x1F0
#define ATA_SECONDARY_IO 0x170
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_SECONDARY_CTRL 0x376

#define ATA_DRIVE_MASTER 0xE0
#define ATA_DRIVE_SLAVE 0xF0

#define ATA_REG_DATA 0
#define ATA_REG_SECCOUNT 2
#define ATA_REG_LBA_LO 3
#define ATA_REG_LBA_MID 4
#define ATA_REG_LBA_HI 5
#define ATA_REG_DRIVE 6
#define ATA_REG_STATUS 7
#define ATA_REG_COMMAND 7

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_BSY 0x80

#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_IDENTIFY 0xEC

// Stop the drive raising INTRQ, we only poll
#define ATA_CTRL_NIEN 0x02

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_IDE 0x01

// Set means channel is PCI native mode, ports come from BARs
#define PCI_IDE_PRIMARY_NATIVE 0x01
#define PCI_IDE_SECONDARY_NATIVE 0x04

// ISA I/O is ~1us/access, so these give ~0.1s probe, ~1s transfer
#define ATA_TIMEOUT_PROBE 100000
#define ATA_TIMEOUT_IO 1000000

struct vector *disk_vector = NULL;

// a pointer to the primary hard disk
// allowing IO directly to the disk from LBA zero onwards
struct disk *disk = NULL;

// a pointer to the virtual disk that contains the primary kernel filesystem
// where kernel files are found
struct disk *primary_fs_disk = NULL;

// Burns ~400ns for drive select to settle, per spec
static void ata_io_delay(uint16_t ctrl_base)
{
    for (int i = 0; i < 4; i++)
    {
        insb(ctrl_base);
    }
}

// 0xFF means empty channel, else old code hung forever
static int ata_wait_not_busy(uint16_t io_base, int timeout)
{
    for (int i = 0; i < timeout; i++)
    {
        unsigned char status = insb(io_base + ATA_REG_STATUS);
        if (status == 0xFF)
        {
            return -EIO;
        }
        if (!(status & ATA_SR_BSY))
        {
            return MARROWOS_ALL_OK;
        }
    }
    return -EIO;
}

static int ata_wait_drq(uint16_t io_base, int timeout)
{
    for (int i = 0; i < timeout; i++)
    {
        unsigned char status = insb(io_base + ATA_REG_STATUS);
        if (status == 0xFF || (status & ATA_SR_ERR))
        {
            return -EIO;
        }
        if (status & ATA_SR_DRQ)
        {
            return MARROWOS_ALL_OK;
        }
    }
    return -EIO;
}

static int ata_identify(uint16_t io_base, uint16_t ctrl_base, uint8_t drive_select)
{
    outb(io_base + ATA_REG_DRIVE, drive_select);
    ata_io_delay(ctrl_base);

    outb(io_base + ATA_REG_SECCOUNT, 0);
    outb(io_base + ATA_REG_LBA_LO, 0);
    outb(io_base + ATA_REG_LBA_MID, 0);
    outb(io_base + ATA_REG_LBA_HI, 0);
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_delay(ctrl_base);

    unsigned char status = insb(io_base + ATA_REG_STATUS);
    if (status == 0x00 || status == 0xFF)
    {
        return -EIO;
    }

    if (ata_wait_not_busy(io_base, ATA_TIMEOUT_PROBE) != MARROWOS_ALL_OK)
    {
        return -EIO;
    }

    // Non-zero signature means ATAPI, we can't read sectors from it
    if (insb(io_base + ATA_REG_LBA_MID) != 0 || insb(io_base + ATA_REG_LBA_HI) != 0)
    {
        return -EIO;
    }

    if (ata_wait_drq(io_base, ATA_TIMEOUT_PROBE) != MARROWOS_ALL_OK)
    {
        return -EIO;
    }

    // Drain identify block so drive isn't left mid-transfer
    for (int i = 0; i < 256; i++)
    {
        insw(io_base + ATA_REG_DATA);
    }

    return MARROWOS_ALL_OK;
}

static int disk_read_sector(uint16_t io_base, uint16_t ctrl_base, uint8_t drive_select, int lba, int total, void *buf)
{
    if (ata_wait_not_busy(io_base, ATA_TIMEOUT_IO) != MARROWOS_ALL_OK)
    {
        return -EIO;
    }

    outb(io_base + ATA_REG_DRIVE, drive_select | ((lba >> 24) & 0x0F));
    ata_io_delay(ctrl_base);

    outb(io_base + ATA_REG_SECCOUNT, (unsigned char)total);
    outb(io_base + ATA_REG_LBA_LO, (unsigned char)(lba & 0xff));
    outb(io_base + ATA_REG_LBA_MID, (unsigned char)((lba >> 8) & 0xff));
    outb(io_base + ATA_REG_LBA_HI, (unsigned char)((lba >> 16) & 0xff));
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    unsigned short *ptr = (unsigned short *)buf;
    for (int b = 0; b < total; b++)
    {
        if (ata_wait_not_busy(io_base, ATA_TIMEOUT_IO) != MARROWOS_ALL_OK)
        {
            return -EIO;
        }

        if (ata_wait_drq(io_base, ATA_TIMEOUT_IO) != MARROWOS_ALL_OK)
        {
            return -EIO;
        }

        for (int word = 0; word < 256; word++)
        {
            *ptr++ = insw(io_base + ATA_REG_DATA);
        }
    }

    return 0;
}

int disk_create_new(int type, uint16_t io_base, uint16_t ctrl_base, uint8_t drive_select, int starting_lba, int ending_lba, size_t sector_size, struct disk **disk_out)
{
    int res = 0;
    struct disk *disk = kzalloc(sizeof(struct disk));
    if (!disk)
    {
        res = -ENOMEM;
        goto out;
    }
    disk->type = type;
    disk->id = vector_count(disk_vector);
    disk->sector_size = sector_size;
    disk->starting_lba = starting_lba;
    disk->ending_lba = ending_lba;
    disk->io_base = io_base;
    disk->ctrl_base = ctrl_base;
    disk->drive_select = drive_select;

    // Not all disks have filesystems its not an error not to have one
    disk->filesystem = fs_resolve(disk);
    if (disk->filesystem)
    {
        char fs_name[11] = {0};
        char primary_drive_fs_name[11] = {0};
        strncpy(primary_drive_fs_name, MARROWOS_KERNEL_FILESYSTEM_NAME, strlen(MARROWOS_KERNEL_FILESYSTEM_NAME));
        // Is the disk the primary disk, lets check
        disk->filesystem->volume_name(disk->fs_private, fs_name, sizeof(fs_name));
        if (strncmp(fs_name, primary_drive_fs_name, sizeof(fs_name)) == 0)
        {
            // Set the primary filesystem disk
            primary_fs_disk = disk;
        }
    }

    if (disk_out)
    {
        *disk_out = disk;
    }
    vector_push(disk_vector, &disk);
out:
    return res;
}

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    outdw(PCI_CONFIG_ADDRESS, address);
    return insdw(PCI_CONFIG_DATA);
}

static void disk_probe_channel(uint16_t io_base, uint16_t ctrl_base)
{
    outb(ctrl_base, ATA_CTRL_NIEN);

    uint8_t drives[2] = {ATA_DRIVE_MASTER, ATA_DRIVE_SLAVE};
    for (int d = 0; d < 2; d++)
    {
        if (ata_identify(io_base, ctrl_base, drives[d]) != MARROWOS_ALL_OK)
        {
            continue;
        }

        struct disk *found = NULL;
        if (disk_create_new(MARROWOS_DISK_TYPE_REAL, io_base, ctrl_base, drives[d], 0, 0, MARROWOS_SECTOR_SIZE, &found) < 0)
        {
            continue;
        }

        if (!disk)
        {
            disk = found;
        }
    }
}

// Real hardware runs PCI native mode, QEMU fakes legacy ports
static int disk_probe_pci_ide_controllers()
{
    int controllers = 0;
    for (int bus = 0; bus < 256; bus++)
    {
        for (int slot = 0; slot < 32; slot++)
        {
            for (int func = 0; func < 8; func++)
            {
                uint32_t id = pci_config_read32(bus, slot, func, 0x00);
                if ((id & 0xFFFF) == 0xFFFF)
                {
                    if (func == 0)
                    {
                        break;
                    }
                    continue;
                }

                uint32_t class_reg = pci_config_read32(bus, slot, func, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                uint8_t subclass = (class_reg >> 16) & 0xFF;
                uint8_t prog_if = (class_reg >> 8) & 0xFF;
                if (class_code != PCI_CLASS_MASS_STORAGE || subclass != PCI_SUBCLASS_IDE)
                {
                    continue;
                }
                controllers++;

                uint16_t primary_io = ATA_PRIMARY_IO;
                uint16_t primary_ctrl = ATA_PRIMARY_CTRL;
                if (prog_if & PCI_IDE_PRIMARY_NATIVE)
                {
                    primary_io = pci_config_read32(bus, slot, func, 0x10) & 0xFFFC;
                    // Device-control reg sits at +2 in the native block
                    primary_ctrl = (pci_config_read32(bus, slot, func, 0x14) & 0xFFFC) + 2;
                }

                uint16_t secondary_io = ATA_SECONDARY_IO;
                uint16_t secondary_ctrl = ATA_SECONDARY_CTRL;
                if (prog_if & PCI_IDE_SECONDARY_NATIVE)
                {
                    secondary_io = pci_config_read32(bus, slot, func, 0x18) & 0xFFFC;
                    secondary_ctrl = (pci_config_read32(bus, slot, func, 0x1C) & 0xFFFC) + 2;
                }

                // A zero BAR means firmware never assigned the channel any ports
                if (primary_io != 0)
                {
                    disk_probe_channel(primary_io, primary_ctrl);
                }
                if (secondary_io != 0)
                {
                    disk_probe_channel(secondary_io, secondary_ctrl);
                }
            }
        }
    }
    return controllers;
}

void disk_search_and_init()
{
    disk_vector = vector_new(sizeof(struct disk *), 4, 0);
    if (!disk_vector)
    {
        return;
    }

    if (disk_probe_pci_ide_controllers() == 0)
    {
        // No PCI IDE controller visible at all: fall back to the fixed ISA ports
        disk_probe_channel(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL);
        disk_probe_channel(ATA_SECONDARY_IO, ATA_SECONDARY_CTRL);
    }
}

size_t disk_total()
{
    return vector_count(disk_vector);
}

struct disk *disk_primary()
{
    return disk;
}

struct disk *disk_primary_fs_disk()
{
    return primary_fs_disk;
}

struct disk *disk_get(int index)
{
    size_t total_disks = vector_count(disk_vector);
    if (index >= (int)total_disks)
    {
        // out of bounds no such disk is loaded
        return NULL;
    }

    struct disk *disk = NULL;
    vector_at(disk_vector, index, &disk, sizeof(disk));
    return disk;
}

int disk_read_block(struct disk *idisk, unsigned int lba, int total, void *buf)
{
    size_t absolute_lba = idisk->starting_lba + lba;
    size_t absolute_ending_lba = absolute_lba + total;
    if (absolute_ending_lba > idisk->ending_lba)
    {
        // Is this the primary disk
        if (idisk->starting_lba != 0 && idisk->ending_lba != 0)
        {
            // Out of bounds, you cannot read over to other virtual disks
            return -EIO;
        }
    }

    return disk_read_sector(idisk->io_base, idisk->ctrl_base, idisk->drive_select, absolute_lba, total, buf);
}

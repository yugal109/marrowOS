#include "pata.h"
#include "status.h"
#include "io/io.h"
#include "memory/heap/kheap.h"
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

static int ata_identify(uint16_t io_base, uint16_t ctrl_base, uint8_t drive_select, uint16_t *identify_out)
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

    // Reading the whole block also leaves the drive out of the transfer
    for (int i = 0; i < 256; i++)
    {
        identify_out[i] = insw(io_base + ATA_REG_DATA);
    }

    return MARROWOS_ALL_OK;
}

static int pata_read_sector(uint16_t io_base, uint16_t ctrl_base, uint8_t drive_select, int lba, int total, void *buf)
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

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    outdw(PCI_CONFIG_ADDRESS, address);
    return insdw(PCI_CONFIG_DATA);
}

// Identify block: model in words 27-46 (high byte first), size in words 60-61,
// or 100-103 when the drive supports 48-bit LBA (word 83 bit 10)
static void pata_disk_info_set(struct disk *disk, const uint16_t *identify)
{
    char model[40];
    for (int i = 0; i < 20; i++)
    {
        model[i * 2] = (char)(identify[27 + i] >> 8);
        model[i * 2 + 1] = (char)(identify[27 + i] & 0xFF);
    }

    uint64_t sectors = (uint64_t)identify[60] | ((uint64_t)identify[61] << 16);
    if (identify[83] & (1u << 10))
    {
        sectors = (uint64_t)identify[100] | ((uint64_t)identify[101] << 16) |
                  ((uint64_t)identify[102] << 32) | ((uint64_t)identify[103] << 48);
    }

    disk_info_set(disk, model, sizeof(model), sectors * PATA_SECTOR_SIZE);
}

static void pata_probe_channel(struct disk_driver *driver, uint16_t io_base, uint16_t ctrl_base)
{
    outb(ctrl_base, ATA_CTRL_NIEN);

    uint8_t drives[2] = {ATA_DRIVE_MASTER, ATA_DRIVE_SLAVE};
    for (int d = 0; d < 2; d++)
    {
        uint16_t identify[256];
        if (ata_identify(io_base, ctrl_base, drives[d], identify) != MARROWOS_ALL_OK)
        {
            continue;
        }

        struct pata_driver_private_data *private_data = kzalloc(sizeof(struct pata_driver_private_data));
        if (!private_data)
        {
            continue;
        }
        private_data->io_base = io_base;
        private_data->ctrl_base = ctrl_base;
        private_data->drive_select = drives[d];

        struct disk *found = NULL;
        if (disk_create_new(driver, NULL, MARROWOS_DISK_TYPE_REAL, 0, 0, PATA_SECTOR_SIZE, private_data, &found) < 0)
        {
            kfree(private_data);
        }
        else
        {
            pata_disk_info_set(found, identify);
        }
    }
}

// Real hardware runs PCI native mode, QEMU fakes legacy ports
static int pata_probe_pci_ide_controllers(struct disk_driver *driver)
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
                    pata_probe_channel(driver, primary_io, primary_ctrl);
                }
                if (secondary_io != 0)
                {
                    pata_probe_channel(driver, secondary_io, secondary_ctrl);
                }
            }
        }
    }
    return controllers;
}

static int pata_driver_read(struct disk *disk, unsigned int lba, int total, void *buf)
{
    struct pata_driver_private_data *private_data = disk_private_data_driver(disk);
    if (!private_data)
    {
        return -EIO;
    }

    return pata_read_sector(private_data->io_base, private_data->ctrl_base, private_data->drive_select, lba, total, buf);
}

static int pata_driver_write(struct disk *disk, unsigned int lba, int total_sectors, void *buf_in)
{
    return -EUNIMP;
}

static int pata_driver_mount(struct disk_driver *driver)
{
    if (pata_probe_pci_ide_controllers(driver) == 0)
    {
        // No PCI IDE controller visible at all: fall back to the fixed ISA ports
        pata_probe_channel(driver, ATA_PRIMARY_IO, ATA_PRIMARY_CTRL);
        pata_probe_channel(driver, ATA_SECONDARY_IO, ATA_SECONDARY_CTRL);
    }
    return 0;
}

static void pata_driver_unmount(struct disk *disk)
{
    struct pata_driver_private_data *private_data = disk_private_data_driver(disk);
    if (private_data)
    {
        kfree(private_data);
    }
}

static int pata_driver_loaded(struct disk_driver *driver)
{
    return 0;
}

static void pata_driver_unloaded(struct disk_driver *driver)
{
    // nothing to do
}

static int pata_driver_mount_partition(struct disk *disk, long starting_lba, long ending_lba, struct disk **partition_disk_out)
{
    int res = 0;
    struct pata_driver_private_data *hardware_private_data = disk_private_data_driver(disk);
    if (!hardware_private_data)
    {
        res = -EINVARG;
        goto out;
    }

    // Partitions share the physical disk's ATA position
    struct pata_driver_private_data *private_data = kzalloc(sizeof(struct pata_driver_private_data));
    if (!private_data)
    {
        res = -ENOMEM;
        goto out;
    }
    *private_data = *hardware_private_data;

    res = disk_create_new(disk->driver, disk, MARROWOS_DISK_TYPE_PARTITION, starting_lba, ending_lba, disk->sector_size, private_data, partition_disk_out);
    if (res < 0)
    {
        kfree(private_data);
    }
out:
    return res;
}

static struct disk_driver pata_driver = {
    .name = "PATA",
    .functions.loaded = pata_driver_loaded,
    .functions.unloaded = pata_driver_unloaded,
    .functions.read = pata_driver_read,
    .functions.write = pata_driver_write,
    .functions.mount = pata_driver_mount,
    .functions.unmount = pata_driver_unmount,
    .functions.mount_partition = pata_driver_mount_partition,
};

struct disk_driver *pata_driver_init()
{
    return &pata_driver;
}

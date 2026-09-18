#ifndef KERNEL_DISK_DRIVERS_PATA_H
#define KERNEL_DISK_DRIVERS_PATA_H

#include <stdint.h>
#include "disk/disk.h"
#include "disk/driver.h"

#define PATA_SECTOR_SIZE 512

struct disk;

// Per-disk ATA position. Real disks own theirs directly; partitions get a
// copy from their hardware disk (io_base/ctrl_base/drive_select never
// differ across a disk and its own partitions).
struct pata_driver_private_data
{
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t drive_select;
};

struct disk_driver *pata_driver_init();

#endif

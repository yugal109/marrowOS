#ifndef DISK_H
#define DISK_H
#include "fs/file.h"
#include "driver.h"
#include <stdint.h>

typedef unsigned int MARROWOS_DISK_TYPE;

// Represent a real physical hard disk
#define MARROWOS_DISK_TYPE_REAL 0

// specifies this disk represents a partition/virtual-disk
#define MARROWOS_DISK_TYPE_PARTITION 1
#define MARROWOS_KERNEL_FILESYSTEM_NAME "MARROW     "

struct disk_driver;
struct disk
{
    MARROWOS_DISK_TYPE type;
    int sector_size;

    // The id of the disk
    int id;

    struct filesystem *filesystem;

    struct disk_driver *driver;

    // the hardware disk this disk is attached too
    struct disk *hardware_disk;

    // set both to zero fro the primary disk
    // all bounds checking is ignored if set to zero.
    size_t starting_lba;
    size_t ending_lba;

    // The private data of our filesystem
    void *fs_private;

    // private data known by the disk driver in relation to the disk
    void *driver_private;
};

struct disk *disk_hardware_disk(struct disk *disk);
int disk_create_new(struct disk_driver *driver, struct disk *hardware_disk, int type, int starting_lba, int ending_lba, size_t sector_size, void *driver_private_data, struct disk **disk_out);
int disk_create_partition(struct disk *disk, int starting_lba, int ending_lba, struct disk **partition_disk_out);
int disk_filesystem_mount(struct disk *disk);
void *disk_private_data_driver(struct disk *disk);
void disk_search_and_init();
size_t disk_total();
struct disk *disk_get(int index);
int disk_read_block(struct disk *idisk, unsigned int lba, int total, void *buf);
struct disk *disk_primary_fs_disk();
struct disk *disk_primary();

#endif

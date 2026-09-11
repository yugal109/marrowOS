#ifndef DISK_H
#define DISK_H
#include "fs/file.h"
#include <stdint.h>

typedef unsigned int MARROWOS_DISK_TYPE;

// Represent a real physical hard disk
#define MARROWOS_DISK_TYPE_REAL 0

// specifies this disk represents a partition/virtual-disk
#define MARROWOS_DISK_TYPE_PARTITION 1
#define MARROWOS_KERNEL_FILESYSTEM_NAME "MARROW     "

struct disk
{
    MARROWOS_DISK_TYPE type;
    int sector_size;

    // The id of the disk
    int id;

    struct filesystem *filesystem;

    // set both to zero fro the primary disk
    // all bounds checking is ignored if set to zero.
    size_t starting_lba;
    size_t ending_lba;

    // Which legacy ATA position this disk lives at. Partitions inherit these
    // from the physical disk they were found on.
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t drive_select;

    // The private data of our filesystem
    void *fs_private;
};

int disk_create_new(int type, uint16_t io_base, uint16_t ctrl_base, uint8_t drive_select, int starting_lba, int ending_lba, size_t sector_size, struct disk **disk_out);
void disk_search_and_init();
size_t disk_total();
struct disk *disk_get(int index);
int disk_read_block(struct disk *idisk, unsigned int lba, int total, void *buf);
struct disk *disk_primary_fs_disk();
struct disk *disk_primary();

#endif

#ifndef DISK_H
#define DISK_H
#include "fs/file.h"

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

    // The private data of our filesystem
    void *fs_private;
};

void disk_search_and_init();
struct disk *disk_get(int index);
int disk_read_block(struct disk *idisk, unsigned int lba, int total, void *buf);
int disk_create_new(int type, int starting_lba, int ending_lba, size_t sector_size, struct disk **disk_out);

#endif

#include "disk.h"
#include "io/io.h"
#include "memory/memory.h"
#include "status.h"
#include "config.h"
#include "kernel.h"
#include "memory/heap/kheap.h"
#include "string/string.h"
#include "lib/vector/vector.h"
#include "disk/streamer.h"
#include <stdint.h>

struct vector *disk_vector = NULL;

// a pointer to the primary hard disk
// allowing IO directly to the disk from LBA zero onwards
struct disk *disk = NULL;

// a pointer to the virtual disk that contains the primary kernel filesystem
// where kernel files are found
struct disk *primary_fs_disk = NULL;

struct disk *disk_hardware_disk(struct disk *disk)
{
    return disk->hardware_disk;
}

int disk_create_partition(struct disk *disk, int starting_lba, int ending_lba, struct disk **partition_disk_out)
{
    return disk_driver_mount_partition(disk->driver, disk, starting_lba, ending_lba, partition_disk_out);
}

int disk_filesystem_mount(struct disk *disk)
{
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

    return 0;
}

void *disk_private_data_driver(struct disk *disk)
{
    return disk->driver_private;
}

long disk_real_sector(struct disk *idisk, unsigned int lba)
{
    size_t absolute_lba = idisk->starting_lba + lba;
    return absolute_lba;
}

long disk_real_offset(struct disk *idisk, unsigned int lba)
{
    size_t absolute_lba = disk_real_sector(idisk, lba);
    return absolute_lba * idisk->sector_size;
}

int disk_create_new(struct disk_driver *driver, struct disk *hardware_disk, int type, int starting_lba, int ending_lba, size_t sector_size, void *driver_private_data, struct disk **disk_out)
{
    int res = 0;
    struct disk *disk = kzalloc(sizeof(struct disk));
    if (!disk)
    {
        res = -ENOMEM;
        goto out;
    }

    if (hardware_disk && type == MARROWOS_DISK_TYPE_REAL)
    {
        res = -EINVARG;
        goto out;
    }

    if (type == MARROWOS_DISK_TYPE_REAL)
    {
        hardware_disk = disk;
    }

    if (hardware_disk == NULL)
    {
        res = -EINVARG;
        goto out;
    }

    if (hardware_disk->type != MARROWOS_DISK_TYPE_REAL)
    {
        res = -EINVARG;
        goto out;
    }

    disk->type = type;
    disk->id = vector_count(disk_vector);
    disk->sector_size = sector_size;
    disk->starting_lba = starting_lba;
    disk->ending_lba = ending_lba;
    disk->driver = driver;
    disk->driver_private = driver_private_data;
    disk->hardware_disk = hardware_disk;
    disk->cache = disk_streamer_cache_new();

    if (disk_out)
    {
        *disk_out = disk;
    }
    vector_push(disk_vector, &disk);
out:
    return res;
}

void disk_search_and_init()
{
    disk_vector = vector_new(sizeof(struct disk *), 4, 0);
    if (!disk_vector)
    {
        return;
    }

    if (disk_driver_system_init() < 0)
    {
        return;
    }

    disk_driver_mount_all();
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

    if (!idisk->driver || !idisk->driver->functions.read)
    {
        return -EIO;
    }

    return idisk->driver->functions.read(idisk, absolute_lba, total, buf);
}

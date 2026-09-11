#include "gpt.h"
#include "disk/disk.h"
#include "status.h"
#include "memory/memory.h"
#include "disk/streamer.h"
#include "kernel.h"

struct disk *gpt_primary_disk = NULL;

size_t gpt_partition_table_header_real_size(struct gpt_partition_table_header *header)
{
    return sizeof(*header) + (header->hdr_size - offsetof(struct gpt_partition_table_header, reserved2));
}

int gpt_partition_table_header_read(struct gpt_partition_table_header *header_out)
{
    int res = 0;
    char sector[gpt_primary_disk->sector_size];
    res = disk_read_block(gpt_primary_disk, GPT_PARTITION_TABLE_HEADER_LBA, 1, sector);
    if (res < 0)
    {
        goto out;
    }

    memcpy(header_out, sector, sizeof(*header_out));
out:
    return res;
}

int gpt_mount_partitions(struct gpt_partition_table_header *partition_header)
{
    int res = 0;
    size_t total_entries = partition_header->total_array_entries;
    uint64_t starting_lba = partition_header->guid_array_lba_start;
    uint64_t starting_byte = starting_lba * gpt_primary_disk->sector_size;
    size_t entry_size = partition_header->array_entry_size;
    struct disk_stream *streamer = disk_streamer_new(gpt_primary_disk->id);
    if (!streamer)
    {
        res = -EINVARG;
        goto out;
    }

    res = disk_streamer_seek(streamer, (int)starting_byte);
    if (res < 0)
    {
        goto out;
    }

    for (size_t i = 0; i < total_entries; i++)
    {
        // Read a single partition entry
        char buffer[entry_size];
        res = disk_streamer_read(streamer, buffer, sizeof(buffer));
        if (res < 0)
        {
            goto out;
        }

        // Cast to access the memory as it truly is
        struct gpt_partition_entry *entry = (struct gpt_partition_entry *)buffer;
        char guid_empty[16] = {0};
        // If the GUID is all zeros then this entry can be skipped
        if (memcmp(entry->guid, guid_empty, sizeof(entry->guid)) == 0)
        {
            // Not a valid partition
            continue;
        }

        // We have the entry, lets create a virtual disk
        res = disk_create_new(MARROWOS_DISK_TYPE_PARTITION, gpt_primary_disk->io_base, gpt_primary_disk->ctrl_base,
                              gpt_primary_disk->drive_select, entry->starting_lba, entry->ending_lba, gpt_primary_disk->sector_size, NULL);
        if (res < 0)
        {
            goto out;
        }
        // WARNING: CODE WONT COMPILE YET, UNTIL WE IMPLEMENT VIRTUAL DISKS
        // IN THE DISK FUNCTIONALITY
        // We have mounted the partition as a virtual disk.
    }
out:
    return res;
}

int gpt_init()
{
    // Snapshot the count first: mounting partitions appends virtual disks to the
    // same vector we are walking.
    size_t total_physical_disks = disk_total();

    for (size_t i = 0; i < total_physical_disks; i++)
    {
        struct disk *physical_disk = disk_get((int)i);
        if (!physical_disk || physical_disk->type != MARROWOS_DISK_TYPE_REAL)
        {
            continue;
        }

        gpt_primary_disk = physical_disk;

        struct gpt_partition_table_header partition_header = {0};
        if (gpt_partition_table_header_read(&partition_header) < 0)
        {
            // Unreadable disk, try the next one
            continue;
        }

        if (memcmp(partition_header.signature, GPT_SIGNATURE, sizeof(partition_header.signature)) != 0)
        {
            // Not a gpt formatted disk
            continue;
        }

        // This is a GPT disk mount all partitions as seperate
        // virtual disks
        gpt_mount_partitions(&partition_header);
    }

    return 0;
}

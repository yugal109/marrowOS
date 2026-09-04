#include "gpt.h"
#include "disk/disk.h"
#include "status.h"
#include "memory/memory.h"
#include "disk/streamer.h"

struct disk *gpt_primary_disk = NULL;

size_t gpt_partition_table_header_real_size(struct gpt_partition_table_header *header)
{
    return sizeof(*header) + (header->hdr_size - offsetof(struct gpt_partition_table_header, reserved2));
}

int gpt_partition_table_header_read(struct gpt_partition_table_header *header_out)
{
    int res = 0;
    /* MAC-QEMU-FIX: fixed 512 buffer + offsetof copy — VLA/sizeof(flexible) is unsafe here */
    char sector[512];
    res = disk_read_block(gpt_primary_disk, GPT_PARTITION_TABLE_HEADER_LBA, 1, sector);
    if (res < 0)
    {
        goto out;
    }
    memcpy(header_out, sector, offsetof(struct gpt_partition_table_header, reserved2));
    /* MAC-QEMU-FIX-END */
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
        /* MAC-QEMU-FIX: fixed-size entry buffer — VLA of entry_size can misalign / blow stack */
        char buffer[sizeof(struct gpt_partition_entry)];
        res = disk_streamer_read(streamer, buffer, (int)entry_size);
        /* MAC-QEMU-FIX-END */
        if (res < 0)
        {
            goto out;
        }

        // case to access the memory as it truly is
        struct gpt_partition_entry *entry = (struct gpt_partition_entry *)buffer;
        char guid_empty[16] = {0};
        // if the guid is all zeros then this entry can be skipped
        if (memcmp(entry->guid, guid_empty, sizeof(entry->guid)) == 0)
        {
            // not a valid partition
            continue;
        }

        // we have the entry, lets create a virtual disk
        res = disk_create_new(MARROWOS_DISK_TYPE_PARTITION, entry->starting_lba, entry->ending_lba, gpt_primary_disk->sector_size, NULL);
        if (res < 0)
        {
            goto out;
        }
    }
out:
    return res;
}

int gpt_init()
{
    int res = 0;
    /* MAC-QEMU-FIX: avoid "= {0}" — GCC movaps #GP if RSP not 16-byte aligned (green forever) */
    char hdr_buf[96];
    struct gpt_partition_table_header *partition_header = (struct gpt_partition_table_header *)hdr_buf;
    memset(hdr_buf, 0, sizeof(hdr_buf));
    /* MAC-QEMU-FIX-END */

    gpt_primary_disk = disk_get(0);

    if (!gpt_primary_disk)
    {
        res = -EINVARG;
        goto out;
    }

    res = gpt_partition_table_header_read(partition_header);
    if (res < 0)
    {
        goto out;
    }

    if (memcmp(partition_header->signature, GPT_SIGNATURE, sizeof(partition_header->signature)) != 0)
    {
        // Not a gpt formatted disk
        res = -EINFORMAT;
        goto out;
    }

    // This is a GPT disk mount all partitions as separate
    // virtual disks
    res = gpt_mount_partitions(partition_header);
    if (res < 0)
    {
        goto out;
    }

out:
    return res;
}

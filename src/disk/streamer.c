#include "streamer.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "config.h"
#include <stdbool.h>

struct disk_stream *disk_streamer_new(int disk_id)
{

    struct disk *disk = disk_get(disk_id);
    if (!disk)
    {
        return 0;
    }

    struct disk_stream *streamer = kzalloc(sizeof(struct disk_stream));
    streamer->pos = 0;
    streamer->disk = disk;
    return streamer;
}

struct disk_stream *disk_streamer_new_from_disk(struct disk *disk)
{
    struct disk_stream *streamer = kzalloc(sizeof(struct disk_stream));
    streamer->pos = 0;
    streamer->disk = disk;
    return streamer;
}

int disk_streamer_seek(struct disk_stream *stream, int pos)
{
    stream->pos = pos;
    return 0;
}

int disk_streamer_read(struct disk_stream *stream, void *out, int total)
{
    /* MAC-QEMU-FIX: iterative read — Linux+QEMU's recursive version blows the stack on ~1MB BMP fread */
    char *out_ptr = (char *)out;
    int remaining = total;

    while (remaining > 0)
    {
        int sector = stream->pos / MARROWOS_SECTOR_SIZE;
        int offset = stream->pos % MARROWOS_SECTOR_SIZE;
        int total_to_read = remaining;
        if ((offset + total_to_read) >= MARROWOS_SECTOR_SIZE)
        {
            total_to_read = MARROWOS_SECTOR_SIZE - offset;
        }

        char buf[MARROWOS_SECTOR_SIZE];
        int res = disk_read_block(stream->disk, sector, 1, buf);
        if (res < 0)
        {
            return res;
        }

        for (int i = 0; i < total_to_read; i++)
        {
            *out_ptr++ = buf[offset + i];
        }

        stream->pos += total_to_read;
        remaining -= total_to_read;
    }

    return 0;
    /* MAC-QEMU-FIX-END */
}

void disk_streamer_close(struct disk_stream *stream)
{
    kfree(stream);
}

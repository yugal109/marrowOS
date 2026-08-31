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

int disk_streamer_seek(struct disk_stream *stream, int pos)
{
    stream->pos = pos;
    return 0;
}

int disk_streamer_read(struct disk_stream *stream, void *out, int total)
{
    if (total <= 0)
        return -1;

    char *outc = out;
    int remaining = total;

    while (remaining > 0)
    {
        int sector = stream->pos / MARROWOS_SECTOR_SIZE;
        int offset = stream->pos % MARROWOS_SECTOR_SIZE;
        int chunk = MARROWOS_SECTOR_SIZE - offset;
        if (chunk > remaining)
        {
            chunk = remaining;
        }

        char buf[MARROWOS_SECTOR_SIZE];
        int res = disk_read_block(stream->disk, sector, 1, buf);
        if (res < 0)
        {
            return res;
        }

        memcpy(outc, buf + offset, chunk);
        outc += chunk;
        stream->pos += chunk;
        remaining -= chunk;
    }
    return 0;
}

void disk_streamer_close(struct disk_stream *stream)
{
    kfree(stream);
}

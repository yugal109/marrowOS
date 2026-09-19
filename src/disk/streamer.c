#include "streamer.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "config.h"
#include "status.h"
#include <stdbool.h>

struct disk_stream_cache *disk_streamer_cache_new()
{
    return kzalloc(sizeof(struct disk_stream_cache));
}

static struct disk_stream_cache_bucket_level1 *disk_streamer_cache_bucket_level1_get(struct disk_stream_cache *cache, int index)
{
    struct disk_stream_cache_bucket_level1 *level1 = NULL;
    if (cache->total <= (size_t)index)
    {
        size_t old_total = cache->total;
        size_t new_total = index + 1;
        size_t new_size = new_total * sizeof(struct disk_stream_cache_bucket_level1 **);

        struct disk_stream_cache_bucket_level1 **new_buckets = krealloc(cache->buckets, new_size);
        if (!new_buckets)
        {
            return NULL;
        }

        memset(new_buckets + old_total, 0, (new_total - old_total) * sizeof(*new_buckets));
        cache->buckets = new_buckets;
        cache->total = new_total;
    }

    level1 = cache->buckets[index];
    if (!level1)
    {
        cache->buckets[index] = kzalloc(sizeof(struct disk_stream_cache_bucket_level1));
        if (!cache->buckets[index])
        {
            return NULL;
        }

        level1 = cache->buckets[index];
        level1->total_buckets = 0;
    }

    return level1;
}

static struct disk_stream_cache_bucket_level2 *disk_streamer_cache_bucket_level2_get(struct disk_stream_cache_bucket_level1 *level1_bucket, int index)
{
    int max_buckets = (sizeof(level1_bucket->buckets) / sizeof(*level1_bucket->buckets));
    if (index >= max_buckets)
    {
        return NULL;
    }

    struct disk_stream_cache_bucket_level2 *level_two = level1_bucket->buckets[index];
    if (!level_two)
    {
        level_two = kzalloc(sizeof(struct disk_stream_cache_bucket_level2));
        if (!level_two)
        {
            return NULL;
        }

        level_two->total_buckets = 0;
        level1_bucket->buckets[index] = level_two;
    }

    return level_two;
}

static struct disk_stream_cache_bucket_level3 *disk_streamer_cache_bucket_level3_get(struct disk_stream_cache_bucket_level2 *level2, int index)
{
    struct disk_stream_cache_bucket_level3 *level3 = NULL;
    int max_buckets = (sizeof(level2->buckets) / sizeof(*level2->buckets));
    if (index >= max_buckets)
    {
        return NULL;
    }

    level3 = level2->buckets[index];
    if (!level3)
    {
        level3 = kzalloc(sizeof(struct disk_stream_cache_bucket_level3));
        if (!level3)
        {
            return NULL;
        }
        level3->total_sectors = 0;
        level2->buckets[index] = level3;
    }
    return level3;
}

static void disk_streamer_cache_bucket_level3_free(struct disk_stream_cache_bucket_level3 *level3)
{
    for (size_t i = 0; i < DISK_STREAM_LEVEL3_SECTORS_ARRAY_SIZE; i++)
    {
        kfree(level3->sectors[i]);
    }
    kfree(level3);
}

static int disk_streamer_cache_round_robin_add(struct disk_stream_cache *cache, struct disk_stream_cache_bucket_level3 *level3)
{
    int index = cache->mem_roundrobin.pos % DISK_STREAM_CACHE_ROUNDROBIN_MAX;
    struct disk_stream_cache_bucket_level3 *old_elem = cache->mem_roundrobin.queue[index];
    if (old_elem)
    {
        old_elem->roundrobin_count--;
        if (old_elem->roundrobin_count == 0)
        {
            disk_streamer_cache_bucket_level3_free(old_elem);
        }
    }

    cache->mem_roundrobin.queue[index] = level3;
    cache->mem_roundrobin.queue[index]->roundrobin_count++;
    cache->mem_roundrobin.pos++;
    return 0;
}

// Keyed by the REAL absolute byte offset on the underlying hardware disk, so
// a partition and its hardware disk share cache entries for the same bytes.
static int disk_streamer_cache_find(struct disk *disk, long pos, struct disk_stream_cache_sector **cache_sector_out)
{
    int res = DISK_STREAMER_CACHE_STATUS_CACHE_FOUND;
    struct disk_stream_cache *cache = disk->cache;
    if (!cache)
    {
        return -ENOENT;
    }

    long level1_size = DISK_STREAM_BUCKET1_BYTE_SIZE(disk->sector_size);
    long level2_size = DISK_STREAM_BUCKET2_BYTE_SIZE(disk->sector_size);
    long level3_size = DISK_STREAM_BUCKET3_BYTE_SIZE(disk->sector_size);

    long level1_bucket = pos / level1_size;
    long pos_in_level1 = pos % level1_size;
    long level2_bucket = pos_in_level1 / level2_size;
    long pos_in_level2 = pos_in_level1 % level2_size;
    long level3_bucket = pos_in_level2 / level3_size;

    struct disk_stream_cache_bucket_level1 *level1 = disk_streamer_cache_bucket_level1_get(cache, level1_bucket);
    if (!level1)
    {
        return -EINVAL;
    }

    struct disk_stream_cache_bucket_level2 *level2 = disk_streamer_cache_bucket_level2_get(level1, level2_bucket);
    if (!level2)
    {
        return -EINVAL;
    }

    struct disk_stream_cache_bucket_level3 *level3 = disk_streamer_cache_bucket_level3_get(level2, level3_bucket);
    if (!level3)
    {
        return -EINVAL;
    }

    long pos_in_level3 = pos_in_level2 % level3_size;
    int byte_offset = (pos_in_level3 % (sizeof(level3->sectors) * disk->sector_size));
    int sector_index = (byte_offset / disk->sector_size);
    if (!level3->sectors[sector_index])
    {
        level3->sectors[sector_index] = kzalloc(sizeof(struct disk_stream_cache_sector));
        if (!level3->sectors[sector_index])
        {
            return -ENOMEM;
        }
        level3->total_sectors++;
        disk_streamer_cache_round_robin_add(cache, level3);
        res = DISK_STREAMER_CACHE_STATUS_NEW_CACHE_ENTRY;
    }

    if (cache_sector_out)
    {
        *cache_sector_out = level3->sectors[sector_index];
    }

    return res;
}

struct disk_stream *disk_streamer_new(int disk_id)
{
    struct disk *disk = disk_get(disk_id);
    if (!disk)
    {
        return 0;
    }

    return disk_streamer_new_from_disk(disk);
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
    // Iterative (not recursive): large files like bkground.bmp need ~2000 sector
    // reads; recursion blows the kernel stack and the load silently fails.
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

        long real_offset = disk_real_offset(stream->disk, sector);
        struct disk_stream_cache_sector *cache_sector = NULL;
        int cache_res = disk_streamer_cache_find(stream->disk, real_offset, &cache_sector);
        if (cache_res < 0)
        {
            return cache_res;
        }

        if (cache_res == DISK_STREAMER_CACHE_STATUS_NEW_CACHE_ENTRY)
        {
            int res = disk_read_block(stream->disk, sector, 1, cache_sector->buf);
            if (res < 0)
            {
                return res;
            }
        }

        for (int i = 0; i < total_to_read; i++)
        {
            *out_ptr++ = cache_sector->buf[offset + i];
        }

        stream->pos += total_to_read;
        remaining -= total_to_read;
    }

    return 0;
}

void disk_streamer_close(struct disk_stream *stream)
{
    kfree(stream);
}

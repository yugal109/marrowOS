#ifndef DISK_STREAMER_H
#define DISK_STREAMER_H
#include "disk.h"

#define DISK_STREAMER_CACHE_STATUS_NEW_CACHE_ENTRY 0x01
#define DISK_STREAMER_CACHE_STATUS_CACHE_FOUND 0x00

#define DISK_STREAMER_MAX_CACHE_SECTOR_SIZE 2048

// 64 cache sectors per bucket
#define DISK_STREAM_LEVEL3_SECTORS_ARRAY_SIZE 64
#define DISK_STREAM_BUCKET_ARRAY_SIZE 1024
#define DISK_STREAM_CACHE_ROUNDROBIN_MAX 1024

#define DISK_STREAM_BUCKET3_BYTE_SIZE(sector_size) (1L * DISK_STREAM_LEVEL3_SECTORS_ARRAY_SIZE * (sector_size))
#define DISK_STREAM_BUCKET2_BYTE_SIZE(sector_size) (1L * DISK_STREAM_BUCKET3_BYTE_SIZE(sector_size) * DISK_STREAM_BUCKET_ARRAY_SIZE)
#define DISK_STREAM_BUCKET1_BYTE_SIZE(sector_size) (1L * DISK_STREAM_BUCKET2_BYTE_SIZE(sector_size) * DISK_STREAM_BUCKET_ARRAY_SIZE)

struct disk_stream_cache_sector
{
    char buf[DISK_STREAMER_MAX_CACHE_SECTOR_SIZE];
};

// 3-level sparse tree so we don't allocate cache memory for LBA ranges we've
// never touched: level1/level2 index into 32GB/32MB spans, level3 holds the
// actual cached sectors (64 per bucket).
struct disk_stream_cache_bucket_level3
{
    struct disk_stream_cache_sector *sectors[DISK_STREAM_LEVEL3_SECTORS_ARRAY_SIZE];
    size_t total_sectors;

    int roundrobin_count;
};

struct disk_stream_cache_bucket_level2
{
    struct disk_stream_cache_bucket_level3 *buckets[DISK_STREAM_BUCKET_ARRAY_SIZE];
    size_t total_buckets;
};

struct disk_stream_cache_bucket_level1
{
    struct disk_stream_cache_bucket_level2 *buckets[DISK_STREAM_BUCKET_ARRAY_SIZE];
    size_t total_buckets;
};

// Fixed-size eviction ring: once full, adding a new level3 bucket frees
// whatever bucket the ring slot it lands on last pointed to.
struct disk_stream_cache_round_robin
{
    struct disk_stream_cache_bucket_level3 *queue[DISK_STREAM_CACHE_ROUNDROBIN_MAX];
    int pos;
};

struct disk_stream_cache
{
    struct disk_stream_cache_bucket_level1 **buckets;
    size_t total;

    struct disk_stream_cache_round_robin mem_roundrobin;
};

struct disk_stream
{
    int pos;
    struct disk *disk;
};

struct disk_stream *disk_streamer_new(int disk_id);
int disk_streamer_seek(struct disk_stream *stream, int pos);
int disk_streamer_read(struct disk_stream *stream, void *out, int total);
void disk_streamer_close(struct disk_stream *stream);
struct disk_stream *disk_streamer_new_from_disk(struct disk *disk);
struct disk_stream_cache *disk_streamer_cache_new();

#endif

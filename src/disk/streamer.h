#ifndef DISK_STREAMER_H
#define DISK_STREAMER_H
#include "disk.h"

struct disk_stream
{
    int pos;
    struct disk *disk;
};

struct disk_stream *disk_streamer_new(int disk_id);
int disk_streamer_seek(struct disk_stream *stream, int pos);
int disk_streamer_read(struct disk_stream *stream, void *out, int total);
void disk_streamer_close(struct disk_stream *stream);

#endif

#include "print.h"
#include "graphics.h"
#include "marrowos.h"
#include "stdlib.h"
#include "memory.h"
#include <stdbool.h>
#include <stdint.h>

// Wire format, little endian:
//   "MIMG", width u16, height u16, encoding u8, reserved u8, payload length u32, payload CRC32 u32
// followed by the payload: raw RGB565 pixels, or runs of (count u16, RGB565 u16).
#define HEADER_SIZE 18
#define ENCODING_RAW 0
#define ENCODING_RLE 1
#define RLE_MAX_RUN 65535
#define SEND_CHUNK 1024
// Sent after the picture; the receiver stops at the payload length and ignores it. It pushes
// the last bytes of the picture out of any buffer they might be sitting in.
#define TRAILER_BYTES 512
#define CRC_POLY 0xEDB88320u

struct stream
{
    bool sending;
    bool failed;
    uint32_t total;
    uint32_t crc;
    uint32_t expected_total;
    int last_percent;
    print_progress_fn progress;
    uint8_t chunk[SEND_CHUNK];
    int chunk_len;
};

static uint32_t crc_update(uint32_t crc, const uint8_t *data, int length)
{
    for (int i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc >> 1) ^ (CRC_POLY & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static bool send_all(const uint8_t *data, int length)
{
    int offset = 0;
    while (offset < length)
    {
        int sent = marrowos_serial_write(data + offset, (unsigned long)(length - offset));
        if (sent <= 0)
        {
            return false;
        }
        offset += sent;
    }
    return true;
}

static void stream_flush(struct stream *s)
{
    if (s->chunk_len > 0 && !s->failed)
    {
        if (!send_all(s->chunk, s->chunk_len))
        {
            s->failed = true;
        }
    }
    s->chunk_len = 0;

    if (s->progress && s->expected_total > 0)
    {
        int percent = (int)(((unsigned long long)s->total * 100) / s->expected_total);
        if (percent >= s->last_percent + 5 || percent == 100)
        {
            s->last_percent = percent;
            s->progress(percent);
        }
    }
}

// The same bytes are produced twice: once to measure length and CRC, once to send
static void emit(struct stream *s, const uint8_t *data, int length)
{
    s->crc = crc_update(s->crc, data, length);
    s->total += (uint32_t)length;

    if (!s->sending || s->failed)
    {
        return;
    }

    for (int i = 0; i < length; i++)
    {
        s->chunk[s->chunk_len++] = data[i];
        if (s->chunk_len == SEND_CHUNK)
        {
            stream_flush(s);
        }
    }
}

static uint16_t to_rgb565(struct framebuffer_pixel p)
{
    return (uint16_t)(((p.red >> 3) << 11) | ((p.green >> 2) << 5) | (p.blue >> 3));
}

static void emit_run(struct stream *s, uint16_t count, uint16_t pixel)
{
    uint8_t bytes[4] = {count & 0xFF, count >> 8, pixel & 0xFF, pixel >> 8};
    emit(s, bytes, sizeof(bytes));
}

static void stream_image(struct stream *s, const struct framebuffer_pixel *pixels, int width, int height, int first_row, int encoding)
{
    s->total = 0;
    s->crc = 0xFFFFFFFFu;

    uint16_t run_pixel = 0;
    uint32_t run_length = 0;
    for (int y = first_row; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            uint16_t pixel = to_rgb565(pixels[y * width + x]);
            if (encoding == ENCODING_RAW)
            {
                uint8_t bytes[2] = {pixel & 0xFF, pixel >> 8};
                emit(s, bytes, sizeof(bytes));
            }
            else if (run_length > 0 && pixel == run_pixel && run_length < RLE_MAX_RUN)
            {
                run_length++;
            }
            else
            {
                if (run_length > 0)
                {
                    emit_run(s, (uint16_t)run_length, run_pixel);
                }
                run_pixel = pixel;
                run_length = 1;
            }
        }
    }

    if (encoding == ENCODING_RLE && run_length > 0)
    {
        emit_run(s, (uint16_t)run_length, run_pixel);
    }
    s->crc = ~s->crc;
}

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = value & 0xFF;
    out[1] = value >> 8;
}

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = value & 0xFF;
    out[1] = (value >> 8) & 0xFF;
    out[2] = (value >> 16) & 0xFF;
    out[3] = value >> 24;
}

// Syscall buffers must be on the stack or the heap; the kernel kills a process that
// passes anything else (a static buffer included), so the send buffers are malloc'd.
static struct stream *stream_state = NULL;
static uint8_t *trailer = NULL;

int print_canvas(const struct framebuffer_pixel *pixels, int width, int height, int first_row, print_progress_fn progress)
{
    int rows = height - first_row;
    if (!pixels || width <= 0 || rows <= 0 || width > 65535 || rows > 65535)
    {
        return -1;
    }

    if (!stream_state)
    {
        stream_state = malloc(sizeof(struct stream));
        trailer = malloc(TRAILER_BYTES);
        if (!stream_state || !trailer)
        {
            return -1;
        }
        memset(trailer, 0, TRAILER_BYTES);
    }

    struct stream *s = stream_state;
    s->sending = false;
    s->failed = false;
    s->chunk_len = 0;
    s->last_percent = -5;
    s->progress = progress;

    // Flat drawings compress well; fall back to raw if this one does not
    int encoding = ENCODING_RLE;
    stream_image(s, pixels, width, height, first_row, encoding);
    uint32_t raw_bytes = (uint32_t)width * (uint32_t)rows * 2;
    if (s->total >= raw_bytes)
    {
        encoding = ENCODING_RAW;
        stream_image(s, pixels, width, height, first_row, encoding);
    }

    uint32_t payload_length = s->total;
    uint32_t payload_crc = s->crc;

    uint8_t header[HEADER_SIZE] = {'M', 'I', 'M', 'G'};
    put_u16(header + 4, (uint16_t)width);
    put_u16(header + 6, (uint16_t)rows);
    header[8] = (uint8_t)encoding;
    header[9] = 0;
    put_u32(header + 10, payload_length);
    put_u32(header + 14, payload_crc);
    if (!send_all(header, HEADER_SIZE))
    {
        return -1;
    }

    s->sending = true;
    s->expected_total = payload_length;
    stream_image(s, pixels, width, height, first_row, encoding);
    stream_flush(s);

    if (!s->failed && !send_all(trailer, TRAILER_BYTES))
    {
        s->failed = true;
    }

    return s->failed ? -1 : (int)(HEADER_SIZE + payload_length);
}

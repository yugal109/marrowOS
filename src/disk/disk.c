#include "disk.h"
#include "io/io.h"
#include "memory/memory.h"
#include "status.h"
#include "config.h"

struct disk disk;

int disk_read_sector(int lba, int total, void *buf)
{
    // lba   = which sector to start reading from (0-indexed) - each sector is 512 bytes long
    // total = how many sectors to read
    // buf   = where in RAM to put the data we read

    outb(0x1F6, (lba >> 24) | 0xE0);
    // Select the PRIMARY MASTER drive, and send the highest 4 bits of the LBA address.
    // 0xE0 = the bit pattern meaning "master drive, LBA mode".
    // (lba >> 24) shifts the LBA right by 24 bits, leaving just its top 4 bits,
    // which get OR'd into this same byte alongside the drive-select bits.

    outb(0x1F2, total);
    // Tell the drive how many sectors we want to read in total.

    outb(0x1F3, (unsigned char)(lba & 0xff));
    // Send the LOWEST 8 bits of the LBA address.
    // (lba & 0xff) masks out everything except the bottom byte.

    outb(0x1F4, (unsigned char)(lba >> 8));
    // Send the NEXT 8 bits of the LBA address (bits 8-15).
    // Shift right by 8, so those bits become the new lowest byte, then send that byte.

    outb(0x1F5, (unsigned char)(lba >> 16));
    // Send the NEXT 8 bits of the LBA address (bits 16-23).
    // Same idea, shifted further.
    // (Combined with the 4 bits sent earlier via 0x1F6, that's a full 28-bit LBA address.)

    outb(0x1F7, 0x20);
    // Send the actual READ command (0x20 = "read sectors").
    // Everything above was just SETUP — this line is what actually tells the
    // drive "now go do it".

    unsigned short *ptr = (unsigned short *)buf;
    // Treat our destination buffer as an array of 16-bit values (words),
    // since we're about to read 2 bytes at a time.
    // ptr will "walk forward" through the buffer as we fill it with data.

    for (int b = 0; b < total; b++)
    {
        // Outer loop: repeat this whole process once for EACH sector we asked for.
        // b counts which sector (of the "total" requested) we're currently on.

        // Wait for the buffer to be ready
        char c = insb(0x1F7);
        // Read the current STATUS byte from the drive (port 0x1F7 doubles as
        // both command port [when writing] and status port [when reading]).

        while (!(c & 0x08))
        {
            c = insb(0x1F7);
        }
        // Keep re-reading the status byte in a loop, doing NOTHING else,
        // until bit 3 (0x08) turns on. Bit 3 = "DRQ" = "data is ready for you
        // to read now". This is the "busy-wait / polling" loop — the CPU just
        // spins here, repeatedly checking, until the drive signals it's ready.

        // Copy from harddisk to memory
        for (int i = 0; i < 256; i++)
        {
            // Inner loop: read exactly 256 WORDS (256 x 2 bytes = 512 bytes
            // = exactly one full sector) from the drive's data port.

            *ptr = insw(0x1F0);
            // Read ONE word (2 bytes) from the drive's DATA port (0x1F0),
            // and store it at wherever "ptr" currently points, inside our buffer.

            ptr++;
            // Move ptr forward by one "unsigned short" (2 bytes), so the
            // NEXT word we read lands in the next slot of the buffer,
            // not overwriting the one we just wrote.
        }
        // After this inner loop finishes, we've fully copied ONE sector's
        // worth of data (512 bytes) into our buffer. The outer loop then
        // goes back and repeats the whole "wait, then read 256 words" process
        // for the NEXT sector, until all "total" sectors have been read.
    }

    return 0;
    // Signal success back to whoever called this function.
}

void disk_search_and_init()
{
    memset(&disk, 0, sizeof(disk));
    disk.type = MARROWOS_DISK_TYPE_REAL;
    disk.sector_size = MARROWOS_SECTOR_SIZE;
}

struct disk *disk_get(int index)
{
    if (index != 0)
    {
        return 0;
    }
    return &disk;
}

int disk_read_block(struct disk *idisk, unsigned int lba, int total, void *buf)
{
    if (idisk != &disk)
    {
        return -EIO;
    }
    return disk_read_sector(lba, total, buf);
}

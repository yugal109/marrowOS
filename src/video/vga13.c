#include "vga13.h"
#include "io/io.h"

#define VGA_MISC_WRITE 0x3C2
#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA 0x3C5
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_GC_INDEX 0x3CE
#define VGA_GC_DATA 0x3CF
#define VGA_AC_INDEX 0x3C0
#define VGA_AC_RESET 0x3DA

/* VGA Mode 13h (320x200x256) register dump */
static uint8_t mode13_registers[] = {
    /* MISC */
    0x63,
    /* SEQ */
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    /* CRTC */
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF,
    /* GC */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,
    0xFF,
    /* AC */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00};

static void vga13_write_registers(uint8_t *regs)
{
    outb(VGA_MISC_WRITE, *regs++);

    for (uint8_t i = 0; i < 5; i++)
    {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, *regs++);
    }

    /* Unlock CRTC registers */
    outb(VGA_CRTC_INDEX, 0x03);
    outb(VGA_CRTC_DATA, insb(VGA_CRTC_DATA) | 0x80);
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, insb(VGA_CRTC_DATA) & ~0x80);
    regs[0x03] |= 0x80;
    regs[0x11] &= ~0x80;

    for (uint8_t i = 0; i < 25; i++)
    {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, *regs++);
    }

    for (uint8_t i = 0; i < 9; i++)
    {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, *regs++);
    }

    for (uint8_t i = 0; i < 21; i++)
    {
        insb(VGA_AC_RESET);
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_INDEX, *regs++);
    }

    insb(VGA_AC_RESET);
    outb(VGA_AC_INDEX, 0x20);
}

void vga13_enter()
{
    vga13_write_registers(mode13_registers);
}

void vga13_put_pixel(int x, int y, uint8_t color)
{
    if (x < 0 || x >= VGA13_WIDTH || y < 0 || y >= VGA13_HEIGHT)
    {
        return;
    }
    VGA13_FRAMEBUFFER[(y * VGA13_WIDTH) + x] = color;
}

void vga13_clear(uint8_t color)
{
    for (int i = 0; i < VGA13_WIDTH * VGA13_HEIGHT; i++)
    {
        VGA13_FRAMEBUFFER[i] = color;
    }
}

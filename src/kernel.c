#include "kernel.h"
#include <stddef.h>
#include <stdint.h>
#include "idt/idt.h"
#include "memory/memory.h"
#include "memory/heap/heap.h"
#include "memory/heap/kheap.h"
#include "memory/paging/paging.h"
#include "io/tsc.h"
#include "io/pci.h"
#include "usb/xhci.h"
#include "usb/ehci.h"
#include "graphics/graphics.h"
#include "graphics/font.h"
#include "graphics/bootfont.h"
#include "io/cpuid.h"
#include "fs/pparser.h"
#include "string/string.h"
#include "disk/streamer.h"
#include "disk/disk.h"
#include "graphics/image/image.h"
#include "graphics/terminal.h"
#include "graphics/windows.h"
#include "disk/gpt.h"
#include "task/process.h"
#include "gdt/gdt.h"
#include "task/tss.h"
#include "fs/file.h"
#include "idt/idt.h"
#include "idt/irq.h"
#include "status.h"
#include "isr80h/isr80h.h"
#include "keyboard/keyboard.h"
#include "mouse/mouse.h"
#include "config.h"

struct terminal *system_terminal = NULL;

void terminal_writechar(char c, char color)
{
    if (!system_terminal)
    {
        return;
    }

    terminal_write(system_terminal, c);
}

void print(const char *str)
{
    size_t len = strlen(str);
    for (int i = 0; i < len; i++)
    {
        terminal_writechar(str[i], 15);
    }
}

void panic(const char *msg)
{
    // If terminal isn't up yet, paint the FB so we don't die on a silent black screen
    struct graphics_info *gi = graphics_screen_info();
    if (gi && gi->framebuffer)
    {
        struct framebuffer_pixel red = {.red = 0xff, .green = 0x00, .blue = 0x00, .reserved = 0};
        for (uint32_t y = 0; y < gi->vertical_resolution; y++)
        {
            for (uint32_t x = 0; x < gi->horizontal_resolution; x++)
            {
                gi->framebuffer[y * gi->pixels_per_scanline + x] = red;
            }
        }
    }
    // The reveal gate drops terminal output until boot finishes; open it so msg shows
    graphics_reveal_enable();

    // Panic before the terminal exists: build one here
    if (!system_terminal && gi)
    {
        struct font *font = font_get_system_font();
        if (font)
        {
            struct framebuffer_pixel white = {.red = 0xff, .green = 0xff, .blue = 0xff, .reserved = 0};
            system_terminal = terminal_create(gi, 0, 0, gi->width, gi->height, font, white, 0);
        }
    }
    print(msg);
    while (1)
    {
    }
}

// extern void problem();

// static struct paging_4gb_chunk *kernel_chunk = 0;

// void panic(const char *msg)
// {
//     print(msg);
//     while (1)
//     {
//     };
// }

// void kernel_page()
// {
//     kernel_registers();
//     paging_switch(kernel_chunk);
// };

// struct gdt gdt_real[MARROWOS_TOTAL_GDT_SEGMENTS];
// struct gdt_structured gdt_structured[MARROWOS_TOTAL_GDT_SEGMENTS] = {
//     {.base = 0x00,
//      .limit = 0x00,
//      .type = 0x00}, // NULL Segment
//     {.base = 0x00,
//      .limit = 0xffffffff,
//      .type = 0x09a}, // Kernel Code Segment
//     {.base = 0x00,
//      .limit = 0xffffffff,
//      .type = 0x092}, // Kernel Data Segment
//     {
//         .base = 0x00,
//         .limit = 0xffffffff,
//         .type = 0xF8}, // User Code Segment
//     {
//         .base = 0x00,
//         .limit = 0xffffffff,
//         .type = 0xF2}, // User Data Segment
//     {paging_switch
//         .base = (uint32_t)&tss,
//         .limit = sizeof(tss),
//         .type = 0xE9}, // TSS Segment

// };

struct tss tss;
extern struct gdt_entry gdt[];

struct paging_desc *kernel_paging_desc = 0;

void kernel_page()
{
    kernel_registers();
    paging_switch(kernel_paging_desc);
}

struct paging_desc *kernel_desc()
{
    return kernel_paging_desc;
}

// ---- Boot screen ----
// Drawn before the font system and disk exist, so it carries its own tiny fonts and does all its
// drawing as plain pixel math. Writes straight to the framebuffer like panic() does, bypassing the
// reveal gate. The background, logo, title and footer are painted once; only the battery is redrawn as boot
// progresses.

// The "MarrowOS" title: a 5x7 font scaled up. One byte per row, bit 4 is the leftmost column
#define BOOT_GLYPH_WIDTH 5
#define BOOT_GLYPH_HEIGHT 7
#define BOOT_TITLE_SCALE 6
#define BOOT_TITLE_LENGTH 8

enum
{
    BOOT_GLYPH_M,
    BOOT_GLYPH_A,
    BOOT_GLYPH_R,
    BOOT_GLYPH_O_SMALL,
    BOOT_GLYPH_W,
    BOOT_GLYPH_O_BIG,
    BOOT_GLYPH_S,
};

static const uint8_t boot_glyphs[][BOOT_GLYPH_HEIGHT] = {
    [BOOT_GLYPH_M] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    [BOOT_GLYPH_A] = {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F},
    [BOOT_GLYPH_R] = {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10},
    [BOOT_GLYPH_O_SMALL] = {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E},
    [BOOT_GLYPH_W] = {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A},
    [BOOT_GLYPH_O_BIG] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    [BOOT_GLYPH_S] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
};

static const uint8_t boot_title[BOOT_TITLE_LENGTH] = {
    BOOT_GLYPH_M, BOOT_GLYPH_A, BOOT_GLYPH_R, BOOT_GLYPH_R,
    BOOT_GLYPH_O_SMALL, BOOT_GLYPH_W, BOOT_GLYPH_O_BIG, BOOT_GLYPH_S};

// Battery: a row of separate blocks inside a rounded outline with a nub on the right
#define BOOT_BATTERY_WIDTH 420
#define BOOT_BATTERY_HEIGHT 38
#define BOOT_BATTERY_RADIUS 9
#define BOOT_BATTERY_BORDER 3
#define BOOT_BATTERY_PADDING 5
#define BOOT_BATTERY_NUB_WIDTH 8
#define BOOT_BATTERY_NUB_HEIGHT 16
#define BOOT_BATTERY_BLOCKS 20
#define BOOT_BATTERY_BLOCK_GAP 4

#define BOOT_LOGO_SIZE 200
#define BOOT_LOGO_GAP 14
#define BOOT_TITLE_GAP 38
#define BOOT_FOOTER_MARGIN 36

static bool boot_screen_ready = false;

static struct framebuffer_pixel boot_color(uint8_t red, uint8_t green, uint8_t blue)
{
    struct framebuffer_pixel color = {.red = red, .green = green, .blue = blue, .reserved = 0};
    return color;
}

static void boot_pixel_put(struct graphics_info *screen_info, int x, int y, struct framebuffer_pixel color)
{
    if (x < 0 || y < 0 || x >= (int)screen_info->horizontal_resolution || y >= (int)screen_info->vertical_resolution)
    {
        return;
    }
    screen_info->framebuffer[y * screen_info->pixels_per_scanline + x] = color;
}

static uint8_t boot_blend(uint8_t from, uint8_t to, int step, int steps)
{
    return (uint8_t)(from + (to - from) * step / steps);
}

// The background: one flat charcoal color. A function so any rectangle of it can be repainted on its own
static struct framebuffer_pixel boot_background_at(struct graphics_info *screen_info, int x, int y)
{
    return boot_color(0x17, 0x17, 0x19);
}

static void boot_background_draw(struct graphics_info *screen_info, int left, int top, int width, int height)
{
    for (int y = top; y < top + height; y++)
    {
        for (int x = left; x < left + width; x++)
        {
            boot_pixel_put(screen_info, x, y, boot_background_at(screen_info, x, y));
        }
    }
}

static void boot_title_draw(struct graphics_info *screen_info, int left, int top, struct framebuffer_pixel color)
{
    for (int letter = 0; letter < BOOT_TITLE_LENGTH; letter++)
    {
        // One blank column between letters
        int letter_x = left + letter * (BOOT_GLYPH_WIDTH + 1) * BOOT_TITLE_SCALE;
        for (int row = 0; row < BOOT_GLYPH_HEIGHT; row++)
        {
            uint8_t bits = boot_glyphs[boot_title[letter]][row];
            for (int col = 0; col < BOOT_GLYPH_WIDTH; col++)
            {
                if (!(bits & (0x10 >> col)))
                {
                    continue;
                }
                for (int dy = 0; dy < BOOT_TITLE_SCALE; dy++)
                {
                    for (int dx = 0; dx < BOOT_TITLE_SCALE; dx++)
                    {
                        boot_pixel_put(screen_info, letter_x + col * BOOT_TITLE_SCALE + dx, top + row * BOOT_TITLE_SCALE + dy, color);
                    }
                }
            }
        }
    }
}

static int boot_text_width(const char *text)
{
    return (int)strlen(text) * BOOTFONT_WIDTH;
}

static void boot_text_draw(struct graphics_info *screen_info, int left, int top, const char *text, struct framebuffer_pixel color)
{
    for (int i = 0; text[i]; i++)
    {
        int index = text[i] - BOOTFONT_FIRST_CHAR;
        if (index < 0 || index >= BOOTFONT_CHAR_COUNT)
        {
            index = '?' - BOOTFONT_FIRST_CHAR;
        }
        for (int row = 0; row < BOOTFONT_HEIGHT; row++)
        {
            uint16_t bits = bootfont_rows[index][row];
            for (int col = 0; col < BOOTFONT_WIDTH; col++)
            {
                if (bits & (1 << (BOOTFONT_WIDTH - 1 - col)))
                {
                    boot_pixel_put(screen_info, left + i * BOOTFONT_WIDTH + col, top + row, color);
                }
            }
        }
    }
}

// True when pixel (x, y) is inside a width x height rectangle with fully rounded corners of the
// given radius. Works in doubled coordinates so pixel centers land on the curve evenly
static bool boot_in_round_rect(int x, int y, int width, int height, int radius)
{
    if (x < 0 || y < 0 || x >= width || y >= height)
    {
        return false;
    }

    int center_x = x < radius ? radius : (x >= width - radius ? width - radius : -1);
    int center_y = y < radius ? radius : (y >= height - radius ? height - radius : -1);
    if (center_x < 0 || center_y < 0)
    {
        return true;
    }

    int dx = 2 * x + 1 - 2 * center_x;
    int dy = 2 * y + 1 - 2 * center_y;
    return dx * dx + dy * dy <= 4 * radius * radius;
}

// The logo: a cross-section of bone. A thick ring (the bone), a thin ring inside it, and a rounded-square
// core (the marrow, the same shape as one battery block). Shapes are tested at 2x2 samples per
// pixel to smooth the edges.
#define BOOT_LOGO_OUTER_RADIUS 80
#define BOOT_LOGO_INNER_RADIUS 64
#define BOOT_LOGO_THIN_OUTER_RADIUS 54
#define BOOT_LOGO_THIN_INNER_RADIUS 49
#define BOOT_LOGO_CORE_HALF 26
#define BOOT_LOGO_CORE_RADIUS 9

#define BOOT_COLOR_LIGHT boot_color(0xe8, 0xe8, 0xe8)
#define BOOT_COLOR_MID boot_color(0x6b, 0x6b, 0x70)

// dx2 and dy2 are in half pixels from the logo's center. Returns false when the sample hits nothing
static bool boot_logo_sample(int dx2, int dy2, struct framebuffer_pixel *color)
{
    int distance_squared = dx2 * dx2 + dy2 * dy2;

    if (boot_in_round_rect(dx2 + BOOT_LOGO_CORE_HALF * 2, dy2 + BOOT_LOGO_CORE_HALF * 2,
                           BOOT_LOGO_CORE_HALF * 4, BOOT_LOGO_CORE_HALF * 4, BOOT_LOGO_CORE_RADIUS * 2))
    {
        *color = BOOT_COLOR_LIGHT;
        return true;
    }

    int thin_outer = BOOT_LOGO_THIN_OUTER_RADIUS * 2;
    int thin_inner = BOOT_LOGO_THIN_INNER_RADIUS * 2;
    if (distance_squared <= thin_outer * thin_outer && distance_squared >= thin_inner * thin_inner)
    {
        *color = BOOT_COLOR_MID;
        return true;
    }

    int outer = BOOT_LOGO_OUTER_RADIUS * 2;
    int inner = BOOT_LOGO_INNER_RADIUS * 2;
    if (distance_squared <= outer * outer && distance_squared >= inner * inner)
    {
        *color = BOOT_COLOR_LIGHT;
        return true;
    }

    return false;
}

static void boot_logo_draw(struct graphics_info *screen_info, int center_x, int center_y)
{
    int half = BOOT_LOGO_SIZE / 2;
    for (int y = -half; y < half; y++)
    {
        for (int x = -half; x < half; x++)
        {
            struct framebuffer_pixel behind = boot_background_at(screen_info, center_x + x, center_y + y);

            int hits = 0;
            int red = 0, green = 0, blue = 0;
            for (int sample = 0; sample < 4; sample++)
            {
                struct framebuffer_pixel sample_color;
                if (boot_logo_sample(x * 2 + (sample & 1), y * 2 + (sample >> 1), &sample_color))
                {
                    hits++;
                    red += sample_color.red;
                    green += sample_color.green;
                    blue += sample_color.blue;
                }
            }

            struct framebuffer_pixel result = behind;
            if (hits > 0)
            {
                // Partly covered pixels fade into what is behind them
                result.red = boot_blend(behind.red, (uint8_t)(red / hits), hits, 4);
                result.green = boot_blend(behind.green, (uint8_t)(green / hits), hits, 4);
                result.blue = boot_blend(behind.blue, (uint8_t)(blue / hits), hits, 4);
            }
            boot_pixel_put(screen_info, center_x + x, center_y + y, result);
        }
    }
}

static void boot_fill_rect(struct graphics_info *screen_info, int left, int top, int width, int height, struct framebuffer_pixel color)
{
    for (int y = top; y < top + height; y++)
    {
        for (int x = left; x < left + width; x++)
        {
            boot_pixel_put(screen_info, x, y, color);
        }
    }
}

// Appends text to a buffer of the given size, always leaving it terminated
static void boot_append(char *buffer, size_t size, const char *text)
{
    size_t length = strlen(buffer);
    for (size_t i = 0; text[i] && length + 1 < size; i++)
    {
        buffer[length++] = text[i];
    }
    buffer[length] = 0;
}

// "MarrowOS v0.1  |  x86_64  |  <cpu>  |  <memory> MB". The CPU name is 48 characters of cpuid text
static void boot_footer_build(char *footer, size_t size)
{
    footer[0] = 0;
    boot_append(footer, size, "MarrowOS v0.1  |  x86_64");

    uint32_t max_leaf = 0, unused_ebx = 0, unused_ecx = 0, unused_edx = 0;
    cpuid(0x80000000, 0, &max_leaf, &unused_ebx, &unused_ecx, &unused_edx);
    if (max_leaf >= 0x80000004)
    {
        char brand[49];
        uint32_t regs[12];
        for (uint32_t i = 0; i < 3; i++)
        {
            cpuid(0x80000002 + i, 0, &regs[i * 4], &regs[i * 4 + 1], &regs[i * 4 + 2], &regs[i * 4 + 3]);
        }
        memcpy(brand, regs, 48);
        brand[48] = 0;

        // Some CPUs pad the name with leading spaces
        char *name = brand;
        while (*name == ' ')
        {
            name++;
        }
        boot_append(footer, size, "  |  ");
        boot_append(footer, size, name);
    }

    boot_append(footer, size, "  |  ");
    boot_append(footer, size, itoa((int)(e820_total_accessible_memory() / (1024 * 1024))));
    boot_append(footer, size, " MB");
}

// One-time part of the screen: background, logo, title and footer
static void boot_screen_draw_static(struct graphics_info *screen_info)
{
    int width = (int)screen_info->horizontal_resolution;
    int height = (int)screen_info->vertical_resolution;

    boot_background_draw(screen_info, 0, 0, width, height);

    int title_width = (BOOT_TITLE_LENGTH * (BOOT_GLYPH_WIDTH + 1) - 1) * BOOT_TITLE_SCALE;
    int title_height = BOOT_GLYPH_HEIGHT * BOOT_TITLE_SCALE;
    int block_height = BOOT_LOGO_SIZE + BOOT_LOGO_GAP + title_height + BOOT_TITLE_GAP + BOOT_BATTERY_HEIGHT;
    int block_top = (height - block_height) / 2;

    boot_logo_draw(screen_info, width / 2, block_top + BOOT_LOGO_SIZE / 2);

    int title_x = (width - title_width) / 2;
    int title_y = block_top + BOOT_LOGO_SIZE + BOOT_LOGO_GAP;
    boot_title_draw(screen_info, title_x + 3, title_y + 3, boot_color(0x08, 0x08, 0x0e));
    boot_title_draw(screen_info, title_x, title_y, boot_color(0xf2, 0xf2, 0xf2));

    char footer[160];
    boot_footer_build(footer, sizeof(footer));
    int footer_x = (width - boot_text_width(footer)) / 2;
    if (footer_x < 0)
    {
        footer_x = 0;
    }
    boot_text_draw(screen_info, footer_x, height - BOOT_FOOTER_MARGIN, footer, boot_color(0x6a, 0x6a, 0x70));
}

// The battery, filled to the given percent. The next block to fill glows dimly, like a charging cell
static void boot_battery_draw(struct graphics_info *screen_info, int left, int top, int percent)
{
    struct framebuffer_pixel border = boot_color(0xb0, 0xb0, 0xb4);
    struct framebuffer_pixel inside = boot_color(0x0e, 0x0e, 0x10);
    struct framebuffer_pixel block_empty = boot_color(0x2a, 0x2a, 0x2e);
    struct framebuffer_pixel block_lit = boot_color(0xe8, 0xe8, 0xe8);
    struct framebuffer_pixel block_next = boot_color(0x70, 0x70, 0x75);

    // Outline and inner panel
    for (int y = 0; y < BOOT_BATTERY_HEIGHT; y++)
    {
        for (int x = 0; x < BOOT_BATTERY_WIDTH; x++)
        {
            if (!boot_in_round_rect(x, y, BOOT_BATTERY_WIDTH, BOOT_BATTERY_HEIGHT, BOOT_BATTERY_RADIUS))
            {
                continue;
            }
            bool in_panel = boot_in_round_rect(x - BOOT_BATTERY_BORDER, y - BOOT_BATTERY_BORDER,
                                               BOOT_BATTERY_WIDTH - BOOT_BATTERY_BORDER * 2,
                                               BOOT_BATTERY_HEIGHT - BOOT_BATTERY_BORDER * 2,
                                               BOOT_BATTERY_RADIUS - BOOT_BATTERY_BORDER);
            boot_pixel_put(screen_info, left + x, top + y, in_panel ? inside : border);
        }
    }

    // The nub on the right end
    boot_fill_rect(screen_info, left + BOOT_BATTERY_WIDTH, top + (BOOT_BATTERY_HEIGHT - BOOT_BATTERY_NUB_HEIGHT) / 2,
                   BOOT_BATTERY_NUB_WIDTH, BOOT_BATTERY_NUB_HEIGHT, border);

    // The blocks
    int area_left = left + BOOT_BATTERY_BORDER + BOOT_BATTERY_PADDING;
    int area_top = top + BOOT_BATTERY_BORDER + BOOT_BATTERY_PADDING;
    int area_width = BOOT_BATTERY_WIDTH - (BOOT_BATTERY_BORDER + BOOT_BATTERY_PADDING) * 2;
    int area_height = BOOT_BATTERY_HEIGHT - (BOOT_BATTERY_BORDER + BOOT_BATTERY_PADDING) * 2;
    int block_width = (area_width - BOOT_BATTERY_BLOCK_GAP * (BOOT_BATTERY_BLOCKS - 1)) / BOOT_BATTERY_BLOCKS;

    int filled = percent * BOOT_BATTERY_BLOCKS / 100;
    for (int block = 0; block < BOOT_BATTERY_BLOCKS; block++)
    {
        struct framebuffer_pixel color = block_empty;
        if (block < filled || percent >= 100)
        {
            color = block_lit;
        }
        else if (block == filled)
        {
            // The block that fills next is half lit
            color = block_next;
        }

        int block_left = area_left + block * (block_width + BOOT_BATTERY_BLOCK_GAP);
        boot_fill_rect(screen_info, block_left, area_top, block_width, area_height, color);
    }
}

void kernel_boot_progress_draw(struct graphics_info *screen_info, int percent)
{
    if (!screen_info || !screen_info->framebuffer)
    {
        return;
    }

    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }

    int width = (int)screen_info->horizontal_resolution;
    int height = (int)screen_info->vertical_resolution;

    if (!boot_screen_ready)
    {
        boot_screen_draw_static(screen_info);
        boot_screen_ready = true;
    }

    int title_height = BOOT_GLYPH_HEIGHT * BOOT_TITLE_SCALE;
    int block_height = BOOT_LOGO_SIZE + BOOT_LOGO_GAP + title_height + BOOT_TITLE_GAP + BOOT_BATTERY_HEIGHT;
    int block_top = (height - block_height) / 2;
    int battery_x = (width - BOOT_BATTERY_WIDTH) / 2;
    int battery_y = block_top + BOOT_LOGO_SIZE + BOOT_LOGO_GAP + title_height + BOOT_TITLE_GAP;
    boot_battery_draw(screen_info, battery_x, battery_y, percent);
}

// Loading here instead of on first click keeps the disk read out of the
// mouse interrupt. dock_slot indexes windows.c's dock_program_paths.
void kernel_preload_dock_app(const char *path, int dock_slot)
{
    struct process *process = NULL;
    int res = process_load_switch(path, &process);
    if (res != MARROWOS_ALL_OK)
    {
        print("Failed to preload: ");
        print(path);
        print("\n");
        return;
    }

    process->dock_slot = dock_slot;
    process->start_hidden = true;
}

// The interrupt code saves and restores each task's x87/SSE registers with fxsave/fxrstor, which
// fault unless CR0.MP is set, CR0.EM is clear and CR4.OSFXSR/OSXMMEXCPT are set. Firmware normally
// leaves it that way, but this does not rely on it
static void kernel_fpu_enable()
{
    uint64_t cr0 = 0;
    uint64_t cr4 = 0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9) | (1ULL << 10);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    __asm__ volatile("fninit");
}

// defined in kernel.asm
extern struct graphics_info default_graphics_info;
void kernel_main()
{
    kernel_fpu_enable();
    tsc_boot_mark();

    struct graphics_info *screen_info = NULL;

    print("Hello 64-bit!\n");

    print("Total memory\n");

    print(itoa(e820_total_accessible_memory()));
    print("\n");

    // memset(gdt_real, 0x00, sizeof(gdt_real));
    // gdt_structured_to_gdt(gdt_real, gdt_structured, MARROWOS_TOTAL_GDT_SEGMENTS);

    // // Load the gdt
    // gdt_load(gdt_real, sizeof(gdt_real) - 1);

    // Initialize the heap
    kheap_init(MARROWOS_HEAP_MINIMUM_SIZE_BYTES);

    char *data = kmalloc(50);
    data[0] = 'A';
    data[1] = 'B';
    data[2] = 'C';
    data[3] = 0x00;
    print(data);

    kernel_paging_desc = paging_desc_new(PAGING_MAP_LEVEL_4);
    if (!kernel_paging_desc)
    {
        panic("Failed to create kernel paging descriptor\n");
    }
    paging_map_e820_memory_regions(kernel_paging_desc);

    // // map the first 419MB to the first 419MB of memory
    // paging_map_range(kernel_paging_desc,
    //                  (void *)0x00000000,                     // virtual address
    //                  (void *)0x00000000,                     // physical address
    //                  1024 * 100,                             // total size
    //                  PAGING_IS_WRITEABLE | PAGING_IS_PRESENT // flags
    // );

    paging_switch(kernel_paging_desc);
    kheap_post_paging();

    // Setup the graphics
    graphics_setup(&default_graphics_info);

    screen_info = graphics_screen_info();

    // Straight to hardware: the boot screen bypasses the reveal gate, which blocks everything
    // else until boot finishes
    kernel_boot_progress_draw(screen_info, 0);

    // Enable interrupt descriptor table
    idt_init();

    // enable pci and scan for devices
    pci_init();
    kernel_boot_progress_draw(screen_info, 15);

    // Enable fs functionality
    fs_init();

    // Enable the disks
    disk_search_and_init();

    // Initialize GPT(gloabl partition table) drives
    gpt_init();
    kernel_boot_progress_draw(screen_info, 35);

    // Initialize the font system
    font_system_init();

    // Setup the terminal system
    terminal_system_setup();
    kernel_boot_progress_draw(screen_info, 50);

    // initialize mouse system
    mouse_system_init();

    // intitalize window system
    window_system_initialize();

    // load the statis mouse dirvers
    mouse_system_load_static_drivers();

    // initialize stage two graphics setup
    graphics_setup_stage_two(&default_graphics_info);
    kernel_boot_progress_draw(screen_info, 65);

    struct font *font = font_get_system_font();
    if (!font)
    {
        panic("Failed to load system font\n");
    }

    struct framebuffer_pixel font_color = {0};
    font_color.red = 0xff;

    // Terminal first (like Daniel) so print/panic are visible
    system_terminal = terminal_create(screen_info, 0, 0, screen_info->width, screen_info->height, font, font_color, TERMINAL_FLAG_BACKSPACE_ALLOWED);
    if (!system_terminal)
    {
        panic("Failed to create system terminal\n");
    }

    // Wallpaper after terminal; refresh background snapshot so glyphs don't punch black holes
    struct image *img = graphics_image_load("@:/bkground.bmp");
    if (!img)
    {
        panic("Failed to load bkground.bmp\n");
    }
    graphics_draw_image_scaled(screen_info, img, 0, 0, screen_info->width, screen_info->height);
    graphics_redraw_all();
    terminal_background_save(system_terminal);

    // After the wallpaper so both appear together. keyboard_init first:
    // stage2 registers a keyboard listener and no-ops without one.
    keyboard_init();
    window_system_initialize_stage2();
    kernel_boot_progress_draw(screen_info, 85);

    // Allocate a 1 MB stack for the kernel IDT
    size_t stack_size = 1024 * 1024;
    void *megabyte_stack_tss_end = kzalloc(stack_size);
    void *megabyte_stack_tss_begin = (void *)(((uintptr_t)megabyte_stack_tss_end) + stack_size);

    // Block the first page
    paging_map(kernel_desc(), megabyte_stack_tss_end, megabyte_stack_tss_end, 0);

    // Setup the TSS
    memset(&tss, 0x00, sizeof(tss));
    tss.rsp0 = (uint64_t)megabyte_stack_tss_begin;
    tss.iopb_offset = sizeof(tss); // No I/O permissions are used

    struct tss_desc_64 *tssdesc = (struct tss_desc_64 *)&gdt[KERNEL_LONG_MODE_TSS_GDT_INDEX];
    gdt_set_tss(tssdesc, &tss, sizeof(tss) - 1, TSS_DESCRIPTOR_TYPE, 0x00);

    // load the tss
    tss_load(KERNEL_LONG_MODE_TSS_SELECTOR);

    // initialize the process system
    process_system_init();

    // Register isr80h commands
    isr80h_register_commands();

    // struct window *win = window_create(graphics_screen_info(), NULL, "Test Window", 50, 50, 300, 300, 0, 4395327);
    // if (win)
    // {
    //     // supresses warnings.
    // }

    // struct window *win = window_create(graphics_screen_info(), NULL, "Test Window", 100, 100, 200, 200, 0, -1);
    // if (!win)
    // {
    //     print("Window creation issue\n");
    // }

    // print("Loading program...\n");
    struct process *process = 0;
    int res = process_load_switch("@:/shell.elf", &process);
    if (res != MARROWOS_ALL_OK)
    {
        panic("Failed to load user program\n");
    }
    kernel_boot_progress_draw(screen_info, 90);

    kernel_preload_dock_app("@:/settings.elf", 1);
    kernel_preload_dock_app("@:/editor.elf", 2);
    kernel_preload_dock_app("@:/calc.elf", 3);
    kernel_preload_dock_app("@:/draw.elf", 5);
    kernel_boot_progress_draw(screen_info, 100);

    // Phase 0 recon: prints straight into the terminal, so it needs to run
    // after the terminal exists and before the reveal, or the reveal gate
    // hides it and the wallpaper draws over it.
    xhci_init();
    ehci_init();

    // Show the desktop only now, as it becomes interactive
    graphics_reveal_enable();
    graphics_redraw_all();

    // Unmask timer IRQ0 or tasks never switch; tick fast enough to poll USB
    idt_timer_frequency_set();
    IRQ_enable(IRQ_TIMER);

    // Drop to user land
    task_run_first_ever_task();

    // struct command_argument argument;
    // argument.next = 0x00;
    // strcpy(argument.argument, "Testing!");

    // process_inject_arguments(process, &argument);

    // int res = process_load_switch("0:/blank.elf", &process);

    // if (res != MARROWOS_ALL_OK)
    // {
    //     panic("Failed to load shell file.\n");
    // }

    // strcpy(argument.argument, "ABC!");
    // argument.next = 0x00;

    // process_inject_arguments(process, &argument);

    // enable the system interrupts
    // enable_interrupts();
}

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
#include "graphics/graphics.h"
#include "graphics/font.h"
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

// Rectangles, not text: no font system this early. Writes straight to
// hardware like panic() does, bypassing the reveal gate.
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

    int bar_width = 320;
    int bar_height = 18;
    int bar_x = ((int)screen_info->horizontal_resolution - bar_width) / 2;
    int bar_y = ((int)screen_info->vertical_resolution - bar_height) / 2;
    int border_thickness = 2;

    struct framebuffer_pixel border = {.red = 0x60, .green = 0x60, .blue = 0x70, .reserved = 0};
    struct framebuffer_pixel empty = {.red = 0x1a, .green = 0x1a, .blue = 0x22, .reserved = 0};
    struct framebuffer_pixel fill = {.red = 0x5b, .green = 0xd6, .blue = 0x7a, .reserved = 0};

    int fill_width = (bar_width - border_thickness * 2) * percent / 100;

    for (int y = 0; y < bar_height; y++)
    {
        for (int x = 0; x < bar_width; x++)
        {
            bool on_border = x < border_thickness || x >= bar_width - border_thickness ||
                              y < border_thickness || y >= bar_height - border_thickness;

            struct framebuffer_pixel color = empty;
            if (on_border)
            {
                color = border;
            }
            else if (x - border_thickness < fill_width)
            {
                color = fill;
            }

            int abs_x = bar_x + x;
            int abs_y = bar_y + y;
            screen_info->framebuffer[abs_y * screen_info->pixels_per_scanline + abs_x] = color;
        }
    }
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

// defined in kernel.asm
extern struct graphics_info default_graphics_info;
void kernel_main()
{
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

    // Straight to hardware, since the reveal gate blocks everything else
    // until boot finishes.
    if (screen_info && screen_info->framebuffer)
    {
        struct framebuffer_pixel loading_bg = {.red = 0x1a, .green = 0x1a, .blue = 0x22, .reserved = 0};
        for (uint32_t y = 0; y < screen_info->vertical_resolution; y++)
        {
            for (uint32_t x = 0; x < screen_info->horizontal_resolution; x++)
            {
                screen_info->framebuffer[y * screen_info->pixels_per_scanline + x] = loading_bg;
            }
        }
    }
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

    kernel_preload_dock_app("@:/editor.elf", 2);
    kernel_preload_dock_app("@:/calc.elf", 3);
    kernel_preload_dock_app("@:/draw.elf", 5);
    kernel_boot_progress_draw(screen_info, 100);

    // Phase 0 recon: prints straight into the terminal, so it needs to run
    // after the terminal exists and before the reveal, or the reveal gate
    // hides it and the wallpaper draws over it.
    xhci_init();

    // Show the desktop only now, as it becomes interactive
    graphics_reveal_enable();
    graphics_redraw_all();

    // unmask timer IRQ0, or tasks never switch
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

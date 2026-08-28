#include "kernel.h"
#include <stddef.h>
#include <stdint.h>
#include "idt/idt.h"
// #include "io/io.h"
#include "memory/memory.h"
#include "memory/heap/heap.h"
#include "memory/heap/kheap.h"
#include "memory/paging/paging.h"
// #include "disk/disk.h"
// #include "fs/pparser.h"
#include "string/string.h"
// #include "disk/streamer.h"
// #include "task/task.h"
// #include "task/process.h"
// #include "gdt/gdt.h"
// #include "task/tss.h"
// #include "fs/file.h"
// #include "idt/idt.h"
#include "status.h"
// #include "isr80h/isr80h.h"
// #include "keyboard/keyboard.h"
#include "config.h"

uint16_t *video_mem = 0;
uint16_t terminal_row = 0;
uint16_t terminal_col = 0;

uint16_t terminal_make_char(char c, char color)
{

    //     // little endian format
    return (color << 8) | c;
}

void terminal_putchar(int x, int y, char c, char color)
{
    video_mem[(y * VGA_WIDTH) + x] = terminal_make_char(c, color);
}

void terminal_backspace()
{
    if (terminal_row == 0 && terminal_col == 0)
    {
        return;
    };
    if (terminal_col == 0)
    {
        terminal_row -= 1;
        terminal_col = VGA_WIDTH;
    };
    terminal_col -= 1;
    terminal_writechar(' ', 15);
    terminal_col -= 1;
}

void terminal_writechar(char c, char color)
{
    if (c == '\n')
    {
        terminal_row += 1;
        terminal_col = 0;
        return;
    }

    // if it's backspace
    if (c == 0x08)
    {
        terminal_backspace();
        return;
    }
    terminal_putchar(terminal_col, terminal_row, c, color);
    terminal_col += 1;
    if (terminal_col >= VGA_WIDTH)
    {
        terminal_col = 0;
        terminal_row += 1;
    }
}

void terminal_initialize()
{
    video_mem = (uint16_t *)(0xB8000);
    terminal_row = 0;
    terminal_col = 0;
    for (int y = 0; y < VGA_HEIGHT; y++)
    {
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            terminal_putchar(x, y, ' ', 0);
        }
    }
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

// struct tss tss;
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

struct paging_desc *kernel_paging_desc = 0;

void kernel_page()
{
    kernel_registers();
    paging_switch(kernel_paging_desc);
}

extern void div_test();

void kernel_main()
{
    terminal_initialize();
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

    idt_init();
    print("hello\n");
    div_test();
    // shouldnt be shown "oi"
    print("oi\n");

    // data[0] = 'M';

    // print(data);

    // struct heap *kernel_heap = kheap_get();
    // size_t total = heap_total_size(kernel_heap);
    // size_t used = heap_total_used(kernel_heap);
    // size_t avail = heap_total_available(kernel_heap);

    // print("\n");
    // print("Total heap size: ");
    // print(itoa(total));
    // print("\n");

    // print("Total heap used: ");
    // print(itoa(used));
    // print("\n");

    // print("Total heap available: ");
    // print(itoa(avail));
    // print("\n");

    // // Initialize the file systems
    // fs_init();

    // // search and initialize the disk
    // disk_search_and_init();

    // // Initialize the interrupt descriptor table
    // idt_init();

    // // Setup the TSS
    // memset(&tss, 0x00, sizeof(tss));
    // tss.esp0 = 0x600000; // this is the kernel stack
    // tss.ss0 = KERNEL_DATA_SELECTOR;

    // // Load the tss
    // tss_load(0x28);

    // // Setup paging
    // kernel_chunk = paging_new_4gb(PAGING_IS_WRITEABLE | PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL);

    // // switch to kernel paging chunk
    // paging_switch(kernel_chunk);

    // // enable paging
    // enable_paging();

    // // register the kernel commands
    // isr80h_register_commands();

    // // Initialize all the system keyboards
    // keyboard_init();

    // struct process *process = 0;
    // int res = process_load_switch("0:/blank.elf", &process);

    // if (res != MARROWOS_ALL_OK)
    // {
    //     panic("Failed to load shell file.\n");
    // }

    // struct command_argument argument;
    // argument.next = 0x00;
    // strcpy(argument.argument, "Testing!");

    // process_inject_arguments(process, &argument);

    // res = process_load_switch("0:/blank.elf", &process);

    // if (res != MARROWOS_ALL_OK)
    // {
    //     panic("Failed to load shell file.\n");
    // }

    // strcpy(argument.argument, "ABC!");
    // argument.next = 0x00;

    // process_inject_arguments(process, &argument);

    // task_run_first_ever_task();

    // enable the system interrupts
    // enable_interrupts();

    while (1)
    {
    }
}

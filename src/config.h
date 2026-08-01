#ifndef CONFIG_H
#define CONFIG_H

#define MARROWOS_TOTAL_INTERRUPTS 512
#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10

// 100MB heap size;
#define MARROWOS_HEAP_SIZE_BYTES 104857600
#define MARROWOS_HEAP_BLOCK_SIZE 4096

// 0x01000000 = 16mb th location in RAM, refer osdev.org/Memory_Map
#define MARROWOS_HEAP_ADDRESS 0x01000000

// refer osdev.org to where this address came from ?
#define MARROWOS_HEAP_TABLE_ADDRESS 0x00007E00

// sector size in normal hard disk
#define MARROWOS_SECTOR_SIZE 512

// total file systems supported by our kernel
#define MARROWOS_MAX_FILESYSTEMS 12

#define MARROWOS_MAX_FILE_DESCRIPTORS 512

#define MARROWOS_MAX_PATH 108

#define MARROWOS_TOTAL_GDT_SEGMENTS 3

#endif

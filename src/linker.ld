ENTRY(_start)
OUTPUT_FORMAT(binary)
SECTIONS
{
    . = 1M;
    .asm : ALIGN(4096)
    {
        *(.asm)
    }
    .text : ALIGN(4096)
    {
        *(.text)
    }
    .rodata : ALIGN(4096)
    {
        *(.rodata)
    }
    .data : ALIGN(4096)
    {
        *(.data)
    }
    /* .bss is NOT stored in a flat binary — it's just an address range that is
       assumed to already be zero. Real hardware leaves firmware/POST garbage in
       RAM, so it must be zeroed explicitly at startup (kernel.asm does this
       using the symbols below). QEMU hands out zeroed RAM, which is why this
       was invisible until booting on a real machine. */
    __bss_start = .;
    .bss : ALIGN(4096)
    {
        *(COMMON)
        *(.bss)
    }
    __bss_end = .;
}

ENTRY(_start)
OUTPUT_FORMAT(binary)
SECTIONS
{
    /* start of user program virtual memory ? remember ?  */
    . = 0x400000; 
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
    .bss : ALIGN(4096)
    {
        *(COMMON)
        *(.bss)
    }
}

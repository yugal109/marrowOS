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
    .bss : ALIGN(4096)
    {
        *(COMMON)
        *(.bss)
    }
   

}

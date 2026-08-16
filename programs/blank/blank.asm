[BITS 32]

section .asm

global _start

_start:
label:
    push message ; Actually pushes the address of message and not the actual value itself, and the address is 4 byte long, hence it actually pushes 4 bytes
    mov eax,1; Command print
    int 0x80

    add esp,4
    jmp $

section .data
message: db 'I can talk with the kernel !', 0

[BITS 64]

global _start
extern c_start
extern marrowos_exit

section .asm

_start:
    call c_start
    call marrowos_exit
    ret


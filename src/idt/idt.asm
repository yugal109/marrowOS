section .asm

extern idt_zero_handler
extern int21h_handler
extern no_interrupt_handler

global idt_load
global idt_zero
global int21h
global no_interrupt
global enable_interrupts
global disable_interrupts

enable_interrupts:
    sti
    ret

disable_interrupts:
    cli
    ret

idt_load:
    push ebp
    mov ebp,esp

    mov ebx,[ebp+8]
    lidt [ebx]

    pop ebp
    ret

idt_zero:
    call idt_zero_handler
    iret

int21h:
    cli
    pushad
    call int21h_handler
    popad
    sti
    iret

no_interrupt:
    cli
    pushad
    call no_interrupt_handler
    popad
    sti
    iret

section .asm

extern idt_zero_handler
extern int21h_handler
extern no_interrupt_handler
extern isr80h_handler

global idt_load
global idt_zero
global int21h
global no_interrupt
global enable_interrupts
global disable_interrupts
global isr80h_wrapper

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
    pushad
    call int21h_handler
    popad
    sti
    iret

no_interrupt:
    pushad
    call no_interrupt_handler
    popad
    sti
    iret

isr80h_wrapper:

    ; INTERRUPT FRAME START => interrupt_frame: raw stack snapshot of CPU state at interrupt time, read via this ESP pointer
    ; ALREADY PUSHED TO US BY THE PROCESSOR UPON ENTRY TO THIS INTERRUPT;
    ; uint32_t ip;
    ; uint32_t cs;
    ; uint32_t flags;
    ; uint32_t sp;
    ; uint32_t ss;
    ; Pushes the general purpose registers to the stack
    pushad

    ; INTERRUPT FRAME END
    ; Push the stack pointer so that we are pointing to the interrupt frame
    push esp

    ; EAX holds our command lets push it to the stack for isr80h_handler 
    push eax
    call isr80h_handler
    mov dword[tmp_res], eax
    add esp, 8


    ; Restore general purpose registers for user land
    popad
    mov eax,[tmp_res]
    iretd

section .data
; Inside here is stored the return result from isr80h_handler
tmp_res:
    dd 0



[BITS 64]

section .asm
global restore_general_purpose_registers
global task_return
global user_registers

; void task_return(struct registers* regs)
task_return: ; (emulating an interrupt here on our own to be able to invoke 'iret/iretd' and enter user land )
    push dword[rdi+88] ; SS
    push qword[rdi+80] ; RSP
    mov rax,[rdi+80]; RSP
    or rax,0x200 ; Set IF Bit
    push rax

    push qword 0x2B ; User data segment
    push qword[rdi+56]; RIP
    call restore_general_purpose_registers 
    add esp, 4

    ; let's leave kernel and go to user land
    iretq

; void restore_general_purpose_registers(struct registers* regs);
restore_general_purpose_registers:
    mov rsi, [rdi+8]
    mov rbp, [rdi+16]
    mov rbx, [rdi+24]
    mov rdx, [rdi+32]
    mov rcx, [rdi+40]
    mov rax, [rdi+48]

    ; Finally RDI
    mov rdi, [rdi]
    ret

; void user_registers()
user_registers:
    mov ax, 0x2B ; User data segment | privileged bit
    mov ds,ax
    mov es,ax
    mov fs,ax
    mov gs,ax
    ret

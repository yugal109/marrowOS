[BITS 64]

section .asm
global restore_general_purpose_registers
global task_return
global user_registers

; void task_return(struct registers* regs)
; rdi = regs (SysV). Fake an interrupt frame, then iretq into ring 3.
; struct registers: 0 rdi, 8 rsi, 16 rbp, 24 rbx, 32 rdx, 40 rcx, 48 rax,
;                   56 ip, 64 cs, 72 flags, 80 rsp, 88 ss
task_return:
    push qword [rdi+88] ; SS
    push qword [rdi+80] ; RSP
    mov rax, [rdi+72]   ; RFLAGS
    or rax, 0x200       ; IF
    push rax
    push qword [rdi+64] ; CS (user code 0x2B, not data)
    push qword [rdi+56] ; RIP
    call restore_general_purpose_registers
    iretq

; void restore_general_purpose_registers(struct registers* regs);
restore_general_purpose_registers:
    mov rsi, [rdi+8]
    mov rbp, [rdi+16]
    mov rbx, [rdi+24]
    mov rdx, [rdi+32]
    mov rcx, [rdi+40]
    mov rax, [rdi+48]
    mov rdi, [rdi]
    ret

; void user_registers()
user_registers:
    mov ax, 0x33 ; USER_DATA_SEGMENT (0x30 | 3). 0x2B is CS.
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ret

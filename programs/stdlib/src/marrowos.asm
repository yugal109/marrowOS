[BITS 64]

section .asm
global print:function
global marrowos_getkey:function
global marrowos_malloc:function
global marrowos_free:function
global marrowos_putchar:function
global marrowos_process_load_start:function
global marrowos_process_get_arguments:function
global marrowos_system: function
global marrowos_exit: function
global marrowos_fopen: function
global marrowos_fclose: function
global marrowos_read: function
global marrowos_fread: function
global marrowos_fseek: function

; void print(const char* message)
print:
    push qword rdi
    mov rax,1 ; Command print
    int 0x80
    add rsp,8
    ret

; int marrowos_getkey()
marrowos_getkey:
    mov rax,2; Command getkey
    int 0x80
    ret

; void putchar(char c);
marrowos_putchar:
    mov rax,3 ; Command putchar
    push qword rdi; Variable "c"
    int 0x80
    add rsp, 8
    ret

; void* marrowos_malloc(size_t size)
marrowos_malloc:
    mov rax,4 ; Command malloc (Allocates memory for the process)
    push qword rdi;  Variable "size" 
    int 0x80
    add rsp, 8
    ret

; void marrowos_free(void* ptr)
marrowos_free:
    mov rax, 5 ; Command 5 free ( Frees the allocated memory for this process)
    push qword rdi; Variable "ptr"
    int 0x80
    add rsp, 8
    ret

; void marrowos_process_load_start(const char* filename);
marrowos_process_load_start:
    mov rax, 6 ; command 6 process load start ( starts a process )
    push qword rdi; Variable "filename"
    int 0x80
    add rsp, 8
    ret

; int marrowos_system(struct command_argument* arguments)
marrowos_system:
    mov rax, 7; Command 7 process_system ( runs a system command based on the arguments)
    push qword rdi; Variable "arguments"
    int 0x80
    add rsp, 8
    ret

; void marrowos_process_get_arguments(struct process_arguments* arguments)
marrowos_process_get_arguments:
    mov rax, 8 ; Command 8 gets the process arguments
    push qword rdi; Variable arguments
    int 0x80
    add rsp, 8
    ret

; void marrowos_exit()
marrowos_exit:
    mov rax,9; Command 9 process exit
    int 0x80
    ret

; int marrowos_fopen(const char* filename,const char* mode)
marrowos_fopen:
    mov rax,10      ;Command 10, fopn
    push qword rsi  ;Pushes the mode
    push qword rdi  ;Push the filename
    int 0x80        ;call thekernel
    add rsp,16      ;restore the stack
    ret 

; void marrowos_fclose(size_t fd);
marrowos_fclose:
    mov rax,11 ; Command 11 fclose
    push qword rdi
    int 0x80
    add rsp, 8 ; restore the stack
    ret

; long marrowos_fread(void* buffer,size_t size,size_t count,long fd);
marrowos_fread:
    mov rax,12; Command 12 read
    push qword rcx; fd
    push qword rdx; count
    push qword rsi; size
    push qword rdi; buffer
    int 0x80; invoke kernel
    add rsp, 32; restore the stack
    ret

; long marrowos_fseek(long fd,long offset,long whence)
marrowos_fseek:
    mov rax,13 ; command 13 fseek
    push qword rdx ; whence
    push qword rsi; offset
    push qword rdi; fd
    int 0x80 ; invoke the kernel
    add rsp,24 ; restores the stack
    ret ;return

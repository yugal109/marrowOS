[BITS 32]

section .asm
global print:function
global marrowos_getkey:function
global marrowos_malloc:function
global marrowos_free:function
global marrowos_putchar:function
global marrowos_process_load_start:function

; void print(const char* message)
print:
    push ebp
    mov ebp,esp

    push dword[ebp+8]
    mov eax,1; Command print
    int 0x80
    add esp,4
    pop ebp
    ret

; int marrowos_getkey()
marrowos_getkey:
    push ebp
    mov ebp, esp
    mov eax,2 ; Command getkey
    int 0x80
    pop ebp
    ret

; void putchar(char c);
marrowos_putchar:
    push ebp
    mov ebp,esp
    mov eax,3 ; Command putchar
    push dword [ebp+8] ; Variable "c"
    int 0x80
    add esp,4
    pop ebp
    ret

; void* marrowos_malloc(size_t size)
marrowos_malloc:
    push ebp
    mov ebp,esp
    mov eax,4 ; Command malloc ( Allocates memory for the process)
    push dword[ebp+8]; Variable "size"
    int 0x80
    add esp,4
    pop ebp
    ret

; void marrowos_free(void* ptr)
marrowos_free:
    push ebp
    mov ebp,esp
    mov eax, 5; Command 5 ( Frees the allocated memory for this process)
    push dword[ebp+8]; Variable "ptr"
    int 0x80
    add esp,4
    pop ebp
    ret

; void marrowos_process_load_start(const char* filename);
marrowos_process_load_start:
    push ebp
    mov ebp,esp
    mov eax,6 ; Command 6 - process load start ( starts a process)
    push dword[ebp+8]; Variable "filename"
    int 0x80
    add esp,4
    pop ebp
    ret

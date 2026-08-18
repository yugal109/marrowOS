[BITS 32]

section .asm
global print:function
global getkey:function
global marrowos_malloc:function
global marrowos_free:function

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

; int getkey()
getkey:
    push ebp
    mov ebp, esp
    mov eax,2 ; Command getkey
    int 0x80
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

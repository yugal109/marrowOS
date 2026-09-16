[BITS 64]

section .asm
global print:   function
global marrowos_getkey: function
global marrowos_malloc: function
global marrowos_free:   function
global marrowos_putchar:    function
global marrowos_process_load_start: function
global marrowos_process_get_arguments:  function
global marrowos_system: function
global marrowos_exit: function
global marrowos_fopen: function
global marrowos_fclose: function
global marrowos_read: function
global marrowos_fread: function
global marrowos_fseek: function
global marrowos_fstat: function
global marrowos_realloc: function
global marrowos_window_create: function
global marrowos_divert_stdout_to_window: function
global marrowos_process_get_window_event:function
global marrowos_window_get_graphics: function
global marrowos_graphic_pixels_get: function
global marrowos_window_redraw:  function
global marrowos_graphics_create:    function
global marrowos_window_redraw_region: function
global marrowos_window_title_set:function
global marrowos_window_cursor_set:function


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

; long marrowos_fstat(long fd,struct file_stat* file_stat_out)
marrowos_fstat:
    mov rax, 14    ; Command 14 fstat
    push qword rsi  ; file_stat_out
    push qword rdi  ; fd
    int 0x80        ; call kernel
    add rsp, 16     ; restore stack
    ret

; void* marrowos_realloc(void* old_ptr,size_t new_size);
marrowos_realloc:
    mov rax, 15     ; Command 15 realloc
    push qword rsi  ; new_size
    push qword rdi  ; old_ptr
    int 0x80
    add rsp, 16 ; RAX = new the pointer address
    ret

; void* marrowos_window_create(const char* title, long width, long height, long flags, long id)
marrowos_window_create:
    mov rax, 16
    push qword R8
    push qword rcx
    push qword rdx
    push qword rsi
    push qword rdi
    int 0x80
    ; restore the stack
    add rsp, 40

    ; RAX = contains the return result
    ret

; void marrowos_divert_stdout_to_window(struct window* window)
marrowos_divert_stdout_to_window:
    mov rax,17; Command 17 - divert stdout to window
    push qword rdi; POinter to userland window
    int 0x80
    add rsp,8
    ret

; int marrowos_process_get_window_event(struct window_event* event);
marrowos_process_get_window_event:
    mov rax, 18 ; Command 18 get window event
    push qword rdi ; The pointer to the window event
    int 0x80
    add rsp, 8
    ; rax < 0 means error or no event
    ret

; void* marrowos_window_get_graphics(struct window* window);
marrowos_window_get_graphics:
    mov rax, 19 ; command 19 get window graphics
    push qword rdi ; the pointer to the window
    int 0x80
    add rsp, 8

    ; rax = struct userland_graphics*
    ret

; void* marrowos_graphic_pixels_get(void* graphics);
marrowos_graphic_pixels_get:
    mov rax, 20   ; Gets the pixel array pointer of a graphic entity
    push qword rdi ; push the graphics ptr.
    int 0x80
    add rsp, 8 
    ret

; void peachos_window_redraw(struct window* window);
marrowos_window_redraw:
    mov rax, 21 ; Redraws the window
    push qword rdi ; push window pointer
    int 0x80
    add rsp, 8 
    ret

; void* marrowos_graphics_create(size_t x, size_t y, size_t width, size_t height, void* parent_graphics);
marrowos_graphics_create:
    mov rax, 22 ; command 22 - create relative graphics
    push qword rdi ; x
    push qword rsi ; y
    push qword rdx ; width
    push qword rcx ; height
    push qword r8 ; parent graphics
    int 0x80
    add rsp, 40 ; restore the stack
    ; rax = contain the new graphics metadata
    ret

; void marrowos_window_redraw_region(long rel_x, long rel_y, long rel_width, long rel_height, struct window* window);
marrowos_window_redraw_region:
    mov rax, 23 ; command 23 redraw region on window
    push qword r8 ; window
    push qword rcx ; rel_height
    push qword rdx ; rel_width
    push qword rsi ; rel_y
    push qword rdi ; rel_x
    int 0x80
    add rsp, 40 ; restore stack
    ret

; void marrowos_window_title_set(struct window* window, const char* title)
marrowos_window_title_set:
    mov rax, 24  ; update window
    push qword rsi ; title
    push qword rdi ; window
    push qword 0 ; update type
    int 0x80
    add rsp, 24 ; restore the stack
    ret

; void marrowos_window_cursor_set(struct window* window, long rel_x, long rel_y)
marrowos_window_cursor_set:
    mov rax, 24   ; update window
    push qword rdx ; rel_y
    push qword rsi ; rel_x
    push qword rdi ; window
    push qword 1  ; update type = ISR80H_WINDOW_UPDATE_CURSOR_POSITION
    int 0x80
    add rsp, 32 ; restore the stack
    ret

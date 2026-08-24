[BITS 64]

section .asm


global paging_load_directory
global paging_invalidate_tlb_entry

; void paging_load_directory(uintptr_t* directory)
paging_load_directory:
    mov rax,rdi; load the first argument (directory) into rax
    mov cr3,rax ; load the page tables pml4 into cr3
    ret

; void paging_invalidate_tlb_entry(void* addr)   
paging_invalidate_tlb_entry:
    invlpg [rdi]
    ret
 
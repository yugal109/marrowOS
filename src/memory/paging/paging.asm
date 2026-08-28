[BITS 64]

section .asm


global paging_load_directory
global paging_invalidate_tlb_entry

; Load CR3 with the PML4 physical address so the CPU uses this 4-level map.
; void paging_load_directory(uintptr_t* directory)
paging_load_directory:
    mov rax,rdi; load the first argument (directory) into rax
    mov cr3,rax ; load the page tables pml4 into cr3
    ret

; Drop one TLB cache line for addr. Needed after we change that page's PT entry.
; void paging_invalidate_tlb_entry(void* addr)
paging_invalidate_tlb_entry:
    invlpg [rdi]
    ret

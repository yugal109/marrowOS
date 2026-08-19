ORG 0x7c00
BITS 16

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

jmp short start
nop


; FAT 16 Header  (BIOS Parameter Block — starts right after jmp/nop at byte 3)

OEMIdentifier     db 'MARROWOS'
; 8-byte OEM name. Not used for mounting math; some tools just display it.
; Must be exactly 8 bytes (pad with spaces if shorter).

BytesPerSector    dw 0x200
; Size of one sector in bytes. 0x200 = 512.
; Almost everything on disk is counted in these units.

SectorsPerCluster db 0x80
; How many sectors make one cluster (FAT's allocation unit).
; 0x80 = 128 sectors → 128 * 512 = 64KB per cluster.
; Files grow in whole clusters.

ReservedSectors   dw 200
; Sectors at the start of the volume BEFORE the first FAT.
; Includes this boot sector (sector 0).
; You use ~199 of these for kernel.bin so FATs start at sector 200.

FATcopies         db 0x02
; Number of File Allocation Table copies.
; 2 = primary FAT + backup (normal FAT12/16).

RootDirEntries    dw 0x40
; Max number of entries in the ROOT directory (FAT16 fixed root).
; 0x40 = 64 entries. Each entry is 32 bytes → root dir size = 64*32 = 2048 bytes = 4 sectors.

NumSectors        dw 0x00
; 16-bit total sector count of the volume.
; 0 means "too big for 16 bits — use SectorsBig instead".

MediaType         db 0xF8
; Media descriptor. 0xF8 = fixed hard disk (not floppy).
; First FAT entry is often related to this value.

SectorsPerFat     dw 0x100
; Size of ONE FAT, in sectors.
; 0x100 = 256 sectors → each FAT is 256 * 512 = 128KB.
; With FATcopies=2, FATs take 512 sectors total.

SectorsPerTrack   dw 0x20
; CHS geometry: sectors per track (old BIOS). 0x20 = 32.
; Mostly unused in pure LBA; still part of the BPB.

NumberOfHeads     dw 0x40
; CHS geometry: number of heads. 0x40 = 64.
; Same — legacy field.

HiddenSectors     dd 0x00
; Sectors before this partition starts (for partitioned disks).
; 0 = volume starts at LBA 0 of the drive/image (your os.bin case).

SectorsBig        dd 0x773594
; 32-bit total sector count (used when NumSectors == 0).
; Defines how large the volume claims to be.


; Extended BPB (DOS 4.0+) — continues the boot sector metadata

DriveNumber       db 0x80
; BIOS drive number. 0x80 = first hard disk.
; 0x00 would be first floppy.

WinNTbit          db 0x00
; Reserved / "dirty" bit used by Windows NT; usually 0.

Signature         db 0x29
; Extended boot signature. 0x29 means the following 3 fields are valid
; (VolumeID, VolumeIDString, SystemIDString).

VolumeID          dd 0xD105
; NOTE: Volume ID is normally a 4-byte (dd) serial number.
; As written this is only 1 byte — usually you'd want: dd 0xD105...
; Serial shown by OS for the volume; not critical for reading FAT.

VolumeIDString    db 'MARROWOSBOO'
; 11-byte volume label (like the name of the disk).
; Must be exactly 11 bytes.

SystemIDString    db 'FAT16   '
; 8-byte FS type string. Must be exactly 8 bytes (space-padded).
; Tells tools/humans this is FAT16. Your driver will still trust the BPB numbers more than this string.







 
start:
    jmp 0:step2

step2:
    cli ; Clear Interrupts
    mov ax, 0x00
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov gs,ax
    mov fs,ax
    mov sp, 0x7c00
    sti ; Enables Interrupts

.load_protected:
    cli
    lgdt[gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:load32

; GDT
gdt_start:

; offset 0x00
gdt_null:
    dd 0x0
    dd 0x0

; offset 0x08
gdt_code:     ; CS SHOULD POINT TO THIS
    dw 0xffff ; Segment limit first 0-15 bits
    dw 0      ; Base first 0-15 bits
    db 0      ; Base 16-23 bits
    db 0x9a   ; Access byte
    db 11001111b ; High 4 bit flags and the low 4 bit flags
    db 0        ; Base 24-31 bits

; offset 0x10
gdt_data:      ; DS, SS, ES, FS, GS
    dw 0xffff ; Segment limit first 0-15 bits
    dw 0      ; Base first 0-15 bits
    db 0      ; Base 16-23 bits
    db 0x92   ; Access byte
    db 11001111b ; High 4 bit flags and the low 4 bit flags
    db 0        ; Base 24-31 bits

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start-1
    dd gdt_start
 
[BITS 32]
load32:
    mov ax,DATA_SEG
    mov es,ax
    mov ds,ax
    mov ss,ax
    mov fs,ax
    mov gs,ax
 
    ; Enable the A20 line
    in al,0x92
    or al,2
    out 0x92, al

    ; For the loading....
    mov eax,1 ; starting sector to load from
    mov ecx,100 ; total sectors we wanna load
    mov edi,0x0100000 ; address where we want to load 


    call ata_lba_read
    jmp CODE_SEG: 0x0100000

ata_lba_read:
    mov ebx,eax ; back up the LBA
    ; send the highest 8 bits of hte lba to hard disk controller
    shr eax,24
    or eax,0xE0 ; select the master drive
    mov dx,0x1F6
    out dx, al
    ; finished sending the highest 8 bits of the lba

    ; send the total sectors to read
    mov eax,ecx
    mov dx,0x1F2
    out dx,al
    ; finished sending the total sectors to read

    ; send more bits of the LBA
    mov eax,ebx; restore the backup LBA
    mov dx,0x1F3
    out dx,al
    ;finished sending more bits of the LBA


    ; send more bits of the LBA
    mov dx,0x1F4
    mov eax,ebx; restore the backup LBA
    shr eax,8
    out dx,al
    ; finished sending more bits of the LBA

    ; send upper 16 bits of the LBA
    mov dx,0x1F5
    mov eax,ebx
    shr eax,16
    out dx,al
    ; finished sending upper 16 bits of the LBA

    mov dx,0x1F7
    mov al,0x20
    out dx,al

    ; read all sectors into memory
.next_sector:
    push ecx

; checking if we need to read
.try_again:
    mov dx,0x1F7
    in al,dx
    test al,8
    jz .try_again

; we need to read 256 words at a time
    mov ecx,256
    mov dx,0x1F0
    rep insw
    pop ecx
    loop .next_sector

    ; end of reading sectors into memory
    ret



times 510-($ - $$) db 0
dw 0xAA55

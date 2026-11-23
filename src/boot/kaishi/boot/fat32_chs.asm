;******************************************************************************
; @file             fat32_chs.asm
;******************************************************************************
%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Memory layout.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Mem.Stage1:                     equ     HEX (7C00)
Mem.Stage2:                     equ     HEX (0600)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Base pointer offsets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define     fat_start                   (bp - 24)
%define     data_start                  (bp - 20)

%define     fat_secmask                 (bp - 16)
%define     fat_secshift                (bp - 14)

;******************************************************************************
; @function        _start
;******************************************************************************
global      _start
_start:

    jmp     short   _after_bpb
    nop

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; System Identifier.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;system_id:                      db      HEX (08)    dup (0)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; BIOS Parameter Block.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;bytes_per_sector:               dw      HEX (0000)
;sectors_per_cluster:            db      HEX (00)
;reserved_sectors:               dw      HEX (0000)
;number_of_fats:                 db      HEX (00)
;root_entries:                   dw      HEX (0000)
;total_sectors16:                dw      HEX (0000)
;media_descriptor:               db      HEX (00)
;sectors_per_fat:                dw      HEX (0000)
;sectors_per_track:              dw      HEX (0000)
;heads_per_cylinder:             dw      HEX (0000)
;hidden_sectors:                 dw      HEX (0000),     HEX (0000)
;total_sectors32:                dw      HEX (0000),     HEX (0000)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Extended BIOS Parameter Block.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;sectors_per_fat32:              dw      HEX (0000),     HEX (0000)
;flags:                          dw      HEX (0000)
;version:                        dw      HEX (0000)
;root_cluster:                   dw      HEX (0000),     HEX (0000)
;info_sector:                    dw      HEX (0000)
;backup_boot:                    dw      HEX (0000)
;reserved2:                      dw      6       dup (0)
;drive_no:                       db      HEX (00)
;boot_flags:                     db      HEX (00)
;ext_boot_sign:                  db      HEX (00)
;volume_id:                      dw      HEX (0000),     HEX (0000)
;volume_label:                   db      HEX (0B)    dup (0)
;fs_type:                        db      HEX (08)    dup (0)


db      HEX (5A) - (. - _start)     dup (0)

%define     _bytes_per_sector           (bp + 11)
%define     _sectors_per_cluster        (bp + 13)
%define     _reserved_sectors           (bp + 14)
%define     _number_of_fats             (bp + 16)
%define     _sectors_per_track          (bp + 24)
%define     _heads_per_cylinder         (bp + 26)
%define     _hidden_sectors             (bp + 28)
%define     _sectors_per_fat32          (bp + 36)
%define     _root_cluster               (bp + 44)
%define     _drive_no                   (bp + 64)
%define     _volume_id                  (bp + 67)

;******************************************************************************
; @function        _after_bpb
;******************************************************************************
_after_bpb:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Disable interrupts and clear the direction flag.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cli
    cld
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Far jump to the next instruction to fix the code segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    jmp    (Mem.Stage1 >> 4) : _real_start

loadseg_60:

    dw      HEX (0000)
    dw      Mem.Stage2 >> 4

;******************************************************************************
; @function        _real_start
;******************************************************************************
_real_start:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the data segment with the code segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    cs
    pop     ds
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the stack segment and the base and stack pointers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     ax,     ax
    mov     ss,     ax
    
    mov     bp,     Mem.Stage1
    lea     sp,     [bp - 64]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Re-enable interrupts.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    sti
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save the drive no. passed to us from them BIOS.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     [_drive_no],    dl
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get drive parameters (HDD only).
    ;; 
    ;; If a drive is moved between different systems, its geometry may
    ;; differ in the eyes of the different BIOSes, more so when it's a
    ;; disk image for a VM.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ah,     HEX (08)                                                    ; clobbers AX, BL, CX, DX, ES:DI
    int     HEX (13)
    jc      .calc_offsets
    
    and     cx,     HEX (3F)
    mov     [_sectors_per_track],       cx
    
    mov     cl,     dh
    inc     cx
    mov     [_heads_per_cylinder],      cx
    
    .calc_offsets:
    
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Calculate the offset of the first FAT.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     si,     [_hidden_sectors]
        mov     di,     [_hidden_sectors + 2]
        
        add     si,     [_reserved_sectors]
        adc     di,     0
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Save the offset ready for the fathelper at the start of kaishi.sys.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     [fat_start],        si
        mov     [fat_start + 2],    di
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Calculate how many sectors both FATs require.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     al,     [_number_of_fats]
        cbw
        push    ax
        
        mul     word ptr [_sectors_per_fat32 + 2]
        add     di,     ax
        
        pop     ax
        mul     word ptr [_sectors_per_fat32]
        
        add     ax,     si
        adc     dx,     di
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Save the offset for later use.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     [data_start],       ax
        mov     [data_start + 2],   dx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Calculate the sector mask.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     ax,     [_bytes_per_sector]
        shr     ax
        shr     ax
        dec     ax
        mov     [fat_secmask],      ax
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Calculate the sector shift.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        xor     cx,     cx
        
        xchg    ax,     cx
        inc     cx
    
    .secshift:
    
        inc     ax
        shr     cx
        
        cmp     cx,     1
        jne     .secshift
        
        mov     [fat_secshift],     ax
        dec     cx
        
        mov     ax,     [_root_cluster]
        mov     dx,     [_root_cluster + 2]
    
    .ff_next_cluster:
    
        push    dx
        push    ax
        
        call    _convert_cluster
        jnc     .ff_next_sector
        
        call    _error
        db      "NF",       HEX (0D),   HEX (0A),   HEX (00)
    
    .ff_next_sector:
    
        push    bx
        les     bx,     [loadseg_60]
        
        push    es
        call    _read_sectors
        
        pop     es
        push    dx
        push    ax
        
        mov     ax,     [_bytes_per_sector]
    
    .ff_next_entry:
    
        mov     si,     offset Entry.Filename
        mov     cx,     offset Entry.Filename.Length
        
        mov     di,     ax
        sub     di,     HEX (20)
        
        repe    cmpsb
        jz      .ff_done
        
        sub     ax,     HEX (20)
        jnz     .ff_next_entry
        
        pop     ax
        pop     dx
        pop     bx
        
        dec     bx
        jnz     .ff_next_sector
    
    .ff_find_next_cluster:
    
        pop     ax
        pop     dx
        
        call    _next_cluster
        jmp     short   .ff_next_cluster
    
    .ff_done:
    
        mov     ax,     es:[di + HEX (1A) - 11]
        mov     dx,     es:[di + HEX (14) - 11]
    
    .c4:
    
        sub     bx,     bx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Get the number of sectors per cluster.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        xor     ch,     ch
        mov     cl,     [_sectors_per_cluster]
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Initialize the di register with the value i the cx register.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     si,     cx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Make sure we read at least 2 sectors.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        cmp     si,     2
        jae     .c5
        
        mov     si,     2
    
    .c5:
    
        push    dx
        push    ax
        push    bx
        
        call    _convert_cluster
        jc      .execute
        
        mov     di,     bx
        sub     si,     bx
        
        pop     bx
    
    .c6:
    
        call    _read_sectors
        
        dec     di
        jnz     .c6
        
        pop     ax
        pop     dx
        
        and     si,     si
        jz      .execute
        
        call    _next_cluster
        jmp     short   .c5
    
    .execute:
    
        mov     cl,     [_drive_no]
        jmp     far ptr [loadseg_60]

;******************************************************************************
; @function        _convert_cluster
;******************************************************************************
_convert_cluster:

    cmp     dx,     HEX (0FFF)
    jne     _convert_cluster.c3
    
    cmp     ax,     HEX (FFF8)
    jb      _convert_cluster.c3
    
    stc
    ret

_convert_cluster.c3:

    mov     cx,     dx
    
    sub     ax,     2
    sbb     cx,     0
    
    mov     bl,     [_sectors_per_cluster]
    dec     bx
    
    sub     bh,     bh
    inc     bx
    
    xchg    cx,     ax
    mul     bx
    
    xchg    ax,     cx
    mul     bx
    
    add     dx,     cx
    
    add     ax,     [data_start]
    add     dx,     [data_start + 2]
    
    ret

;******************************************************************************
; @function        _error
;******************************************************************************
_error:

    pop     si
    jmp     short   _error.next

_error.print:

    mov     ah,     HEX (0E)
    mov     bx,     HEX (0007)
    int     HEX (10)

_error.next:

    lodsb
    
    and     al,     al
    jnz     _error.print

_error.done:

    xor     ax,     ax
    int     HEX (16)
    int     HEX (19)

;******************************************************************************
; @function        _next_cluster
;******************************************************************************
_next_cluster:

    mov     di,     ax
    and     di,     [fat_secmask]
    
    mov     cx,     [fat_secshift]

_next_cluster.cn_loop:

    shr     dx
    rcr     ax
    loop    _next_cluster.cn_loop
    
    shl     di
    shl     di
    
    add     ax,     [fat_start]
    adc     dx,     [fat_start + 2]

_next_cluster.cn1:

    push    es
    push    bx
    
    ;mov     bx,     (Mem.Stage2 >> 4) - HEX (20)
    ;mov     es,     bx
    ;xor     bx,     bx
    
    call    _read_sectors

_next_cluster.cn_exit:

    pop     bx
    pop     es
    
    mov     ax,     es:[di]
    mov     dx,     es:[di + 2]
    
    ret

;******************************************************************************
; @function        _read_sectors
;******************************************************************************
_read_sectors:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the ax and dx registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ax
    push    dx
    
    div     word ptr [_sectors_per_track]
            ; ax = LBA / SPT
            ; dx = LBA % SPT         = sector - 1
    
    xchg    cx,     dx
    inc     cx
            ; cx = sector no.
    
    xor     dx,     dx
    div     word ptr [_heads_per_cylinder]
            ; ax = (LBA / SPT) / HPC = cylinder
            ; dx = (LBA / SPT) % HPC = head
    
    mov     ch,     al
            ; ch = LSB 0...7 of cylinder no.
    
    ror     ah
    ror     ah                                                                  ; excess bits of cylinder no. must be 0 anyway
    
    or      cl,     ah
            ; cl = MSB 8...9 of cylinder no. + sector no.
    
    mov     dh,     dl
            ; dh = head no.
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Read the sector into es:bx.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     HEX (0201)
    mov     dl,     [_drive_no]
    int     HEX (13)
    jnc     _read_sectors.success                                                   ; CF = 0 if no _error

_read_sectors.error:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Invoke the BIOS.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    int     HEX (18)

_read_sectors.success:

    mov     ax,     [_bytes_per_sector]
    mov     cl,     4
    shr     ax,     cl
    
    mov     cx,     es
    add     cx,     ax
    mov     es,     cx

_read_sectors.no_incr:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the ax and dx registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     dx
    pop     ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Advance to the next sector.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     ax,     1
    adc     dx,     0

_read_sectors.done:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return to caller.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Data area.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Entry.Filename:                 db      "KAISHI     "
Entry.Filename.Length:          equ     ($ - Entry.Filename)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Pad until 510 bytes.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
db      HEX (01FE) - (. - _start)   dup (0)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Boot signature.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
dw      HEX (AA55)

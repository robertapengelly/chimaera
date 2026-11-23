;******************************************************************************
; @file             oldfat.asm
;******************************************************************************
%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Memory layout.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Mem.Stage1:                     equ     HEX (7C00)
Mem.Stage2:                     equ     HEX (0600)

;******************************************************************************
; @function        _start
;******************************************************************************
global      _start
_start:

    jmp     short   after_bpb
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
;drive_no:                       db      HEX (00)
;boot_flags:                     db      HEX (00)
;ext_boot_sign:                  db      HEX (00)
;volume_id:                      dw      HEX (0000),     HEX (0000)
;volume_label:                   db      HEX (0B)    dup (0)
;fs_type:                        db      HEX (08)    dup (0)


db      HEX (3E) - (. - _start)     dup (0)

%define     _bytes_per_sector           (bp + 11)
%define     _sectors_per_cluster        (bp + 13)
%define     _reserved_sectors           (bp + 14)
%define     _number_of_fats             (bp + 16)
%define     _root_entries               (bp + 17)
%define     _sectors_per_fat            (bp + 22)
%define     _sectors_per_track          (bp + 24)
%define     _heads_per_cylinder         (bp + 26)
%define     _hidden_sectors             (bp + 28)
%define     _drive_no                   (bp + 36)
%define     _volume_id                  (bp + 39)

;******************************************************************************
; @function        after_bpb
;******************************************************************************
after_bpb:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Disable interrupts and clear the direction flag.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cli
    cld
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Far jump to the next instruction to fix the code segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    jmp    (Mem.Stage1 >> 4) : real_start

;******************************************************************************
; @function        real_start
;******************************************************************************
real_start:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the data segment with the code segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     cs
    mov     ds,     ax
    
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
    test    dl,     HEX (80)
    jz      .calc_offsets                                                       ; skip if it's a flopy (dl < 80h)
    
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
        mov     [bp - 24],      si
        mov     [bp - 22],      di
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Calculate how many sectors both FATs require.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     al,     [_number_of_fats]
        cbw
        mul     word ptr [_sectors_per_fat]
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Adjust the FAT offset to get the root directory offset.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        add     si,     ax
        adc     di,     dx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; As we'll only need the root directory in this file
        ;; we'll just push the offset to the stack.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        push    si
        push    di
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Calculate how many sectors the root directory requires.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     ax,     HEX (0020)
        mul     word ptr [_root_entries]
        div     word ptr [_bytes_per_sector]
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Adjust the root offset to get the data offset.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        add     si,     ax
        adc     di,     dx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Save the offset for later use.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     [bp - 20],      si
        mov     [bp - 18],      di
    
    .find_stage2:
    
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; The ax register should contain the total amount of sectors
        ;; of the root directory.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     cx,     ax
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Get the root directory offset off the stack.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        pop     dx
        pop     ax
        
        .find_stage2.read:
        
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; We'll read the sector into memory 0000:0500.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            mov     bx,     HEX (0050)
            mov     es,     bx
            xor     bx,     bx
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Read the root directory in it's entirety.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            push    es
            push    bx
            call    _read_sectors
            
            pop     bx
            pop     es
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; The rep cmpsb uses di instead of bx.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            mov     di,     bx
            mov     dx,     [_root_entries]
        
        .find_stage2.compare:
        
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Get the file attributes.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            mov     al,     es:[di + 11]
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Mask the value.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            and     al,     HEX (7F)
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Make sure we're not a long filename entry.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            cmp     al,     15
            je      .find_stage2.advance
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Make sure we're a ARCHIVE entry.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            test    al,     HEX (18)
            jnz     .find_stage2.advance
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Save the current offset.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            push    di
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Initialize the si and cx with out entry values.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            mov     si,     offset Entry.Filename
            mov     cx,     offset Entry.Filename.Length
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Perform the compare.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            repe    cmpsb
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Restore the offset.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            pop     di
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; If the entries match then we've found our entry.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            je      .entry_found
        
        .find_stage2.advance:
        
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Advance to the next offset.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            add     di,     32
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; Decrement the counter.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            dec     dx
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; If we haven't read all th entries then compare this entry.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            jnz     .find_stage2.compare
            
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            ;; If we reach here we failed to find our entry.
            ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
            jmp     .error
    
    .entry_found:
    
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Get the starting cluster into the si register.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     ax,     es:[di + 26]
        mov     dx,     es:[di + 20]
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Get the number of sectors per cluster.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        xor     ch,     ch
        mov     cl,     [_sectors_per_cluster]
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Initialize the di register with the value i the cx register.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     di,     cx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Make sure we read at least 2 sectors.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        cmp     di,     2
        jae     .got_count
        
        mov     di,     2
    
    .got_count:
    
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; We'll read the sector/cluster into Mem.Stage2.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     bx,     (Mem.Stage2 >> 4)
        mov     es,     bx
        xor     bx,     bx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Preserve the extra segment and the bx register.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        push    es
        push    bx
    
    .read_entry:
    
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Preserve the ax, dx and bx registers
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        push    dx
        push    ax
        push    bx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Convert the cluster to LBA.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        call    _convert_cluster
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Move the value in the bx register to cx (sectors per cluster)
        ;; and restore the bx register (offset to read into).
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     cx,     bx
        pop     bx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; If the carry flag is set then we've reached the end of the file.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        jc      .execute
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Decrement our sectors count.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        sub     di,     cx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Read the cluster into memory.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        call    _read_sectors
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; If di is zero the we've read all the sectors we need.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        and     di,     di
        jz      .execute
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Restore the ax and dx registers.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        pop     ax
        pop     dx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Get the next cluster.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        call    _next_cluster
        jmp     short   .read_entry
    
    .execute:
    
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Restore the ax, dx and bx registers.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        pop     ax
        pop     dx
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; We'll pass the drive no. in cl.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        mov     cl,     [_drive_no]
        
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Jump to the new code.
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        retf
    
    .error:
    
        call    _error
        db      "NF",       HEX (0D),   HEX (0A),   HEX (00)

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

%ifdef      __FAT12__
;******************************************************************************
; @function        _convert_cluster
;******************************************************************************
_convert_cluster:

    cmp     ax,     HEX (0FF8)
    jb      _convert_cluster.c3
    
    stc
    ret

_convert_cluster.c3:

    xor     dx,     dx
    
    sub     ax,     2
    sbb     dx,     0
    
    mov     bl,     [_sectors_per_cluster]
    dec     bx
    
    sub     bh,     bh
    inc     bx
    
    mul     bx
    
    add     ax,     [bp - 20]
    add     dx,     [bp - 18]
    
    ret

;******************************************************************************
; @function        _next_cluster
;******************************************************************************
_next_cluster:

    push    cx
    push    si
    
    mov     si,     ax

_next_cluster.secoff:

    mov     ax,     3
    mul     si
    shr     ax
    
    push    cx
    pushf
    
    mov     cx,     [_bytes_per_sector]
    shl     cx
    
    xor     dx,     dx
    div     cx
    
    push    dx
    xchg    si,     ax

_next_cluster.read_fat:

    push    es
    push    bx
    
    mov     ax,     [bp - 24]
    mov     dx,     [bp - 22]
    
    add     ax,     si
    adc     dx,     0
    
    mov     cx,     2
    call    _read_sectors
    
    pop     bx
    pop     es

_next_cluster.get_clust:

    pop     si
    popf
    pop     cx
    
    push    ds
    mov     ax,     es
    mov     ds,     ax
    mov     ax,     [si]
    pop     ds
    jnc     _next_cluster.even_cluster
    
    mov     cl,     4
    shr     ax,     cl

_next_cluster.even_cluster:

    and     ax,     HEX (0FFF)

_next_cluster.compare:

    cmp     ax,     HEX (0FF8)
    cmc

_next_cluster.done:

    xor     dx,     dx
    
    pop     si
    pop     cx
    ret
%endif

%ifdef      __FAT16__
;******************************************************************************
; @function        _convert_cluster
;******************************************************************************
_convert_cluster:

    cmp     ax,     HEX (FFF8)
    jb      _convert_cluster.c3
    
    stc
    ret

_convert_cluster.c3:

    xor     dx,     dx
    
    sub     ax,     2
    sbb     dx,     0
    
    mov     bl,     [_sectors_per_cluster]
    dec     bx
    
    sub     bh,     bh
    inc     bx
    
    mul     bx
    
    add     ax,     [bp - 20]
    add     dx,     [bp - 18]
    
    ret

;******************************************************************************
; @function        _next_cluster
;******************************************************************************
_next_cluster:

    push    cx
    push    si

_next_cluster.secoff:

    shl     ax
    
    push    cx
    pushf
    
    mov     cx,     [_bytes_per_sector]
    shl     cx
    
    xor     dx,     dx
    div     cx
    
    push    dx
    xchg    si,     ax

_next_cluster.read_fat:

    push    es
    push    bx
    
    mov     ax,     [bp - 24]
    mov     dx,     [bp - 22]
    
    add     ax,     si
    adc     dx,     0
    
    mov     cx,     2
    call    _read_sectors
    
    pop     bx
    pop     es

_next_cluster.get_clust:

    pop     si
    popf
    pop     cx
    
    mov     ax,     es
    jnc     _next_cluster.first64
    
    add     ax,     HEX (1000)

_next_cluster.first64:

    push    ds
    mov     ds,     ax
    mov     ax,     [si]
    pop     ds

_next_cluster.compare:

    cmp     ax,     HEX (FFF8)
    cmc

_next_cluster.done:

    xor     dx,     dx
    
    pop     si
    pop     cx
    ret
%endif

;******************************************************************************
; @function        _read_sectors
;******************************************************************************
_read_sectors:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the registers that will get clobbered.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    si
    push    di

_read_sectors.next:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; The di regsiter will be our retry counter.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     di,     HEX (0005)

_read_sectors.retry:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the ax, cx and dx registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ax
    push    cx
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
    stc
    int     HEX (13)
    jnc     _read_sectors.success                                                   ; CF = 0 if no error

_read_sectors.failure:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Reset the disk.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     ah,     ah
    mov     dl,     [_drive_no]
    int     HEX (13)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the ax, cx and dx registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     dx
    pop     cx
    pop     ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Decrement the loop counter and retry if greater than zero.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    dec     di
    jnz     _read_sectors.retry
    
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
    ;; Restore the ax, cx and dx registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     dx
    pop     cx
    pop     ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Advance to the next sector.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     ax,     1
    adc     dx,     0
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Loop until cx is zero.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    loop    _read_sectors.next

_read_sectors.done:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the registers that got clobbered.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     di
    pop     si
    
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

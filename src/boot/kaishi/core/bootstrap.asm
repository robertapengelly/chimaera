;******************************************************************************
; @file             bootstrap.asm
;******************************************************************************
.option     segment:use16                                                       ; Make sure we start in 16-bit mode.
.8086                                                                           ; Make sure we're targeting Intel 8086.

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Memory layout.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Mem.Stack.Top:                      equ     HEX (00000840)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Define some offsets for the BPB.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define     _bytes_per_sector           (bp - 64)
%define     _sectors_per_cluster        (bp - 62)
%define     _reserved_sectors           (bp - 61)
%define     _number_of_fats             (bp - 59)
%define     _root_entries               (bp - 58)
%define     _total_sectors16            (bp - 56)
%define     _media_descriptor           (bp - 54)
%define     _sectors_per_fat            (bp - 53)
%define     _sectors_per_track          (bp - 51)
%define     _heads_per_cylinder         (bp - 49)
%define     _hidden_sectors             (bp - 47)
%define     _total_sectors32            (bp - 43)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; FAT32 EBPB offsets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define     _sectors_per_fat32          (bp - 39)
%define     _root_cluster               (bp - 35)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Other offsets used within this file.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define     _edd_available              (bp - 28)
%define     _drive_no                   (bp - 26)

%define     _fat_start                  (bp - 24)
%define     _data_start                 (bp - 20)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; FAT32 offsets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define     _fat_secmask                (bp - 16)
%define     _fat_secshift               (bp - 14)
%define     _fat_sector                 (bp - 12)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Offsets of FAT12 and FAT16 convert cluster and next cluster functions.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define     _convert_cluster            (bp - 8)
%define     _next_cluster               (bp - 6)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Somewhere to store the volume id.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define     _signature                  (bp - 4)

;******************************************************************************
; @function        _start
;******************************************************************************
_start:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Disable interrupts and clear the direction flag.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cli
    cld
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the ax and cx register as well as the flags.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ax
    push    cx
    pushf
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the convertional memory (in KBs) ...
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    int     HEX (12)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ... And convert it into 16-byte paragraphs.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     cl,     6
    shl     ax,     cl
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Reserve memory for a stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     cx,     (Mem.Stack.Top >> 4)
    sub     ax,     cx
    
    mov     di,     ax
    
    popf
    pop     cx
    pop     ax
    
    mov     ss,     di
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the base and stack pointers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     bp,     Mem.Stack.Top
    lea     sp,     [bp - 64]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Re-enable interrupts.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    sti
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save the drive no. passed to us.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     [_drive_no],    cl
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the ax, bx, cx and dx registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ax
    push    bx
    push    cx
    push    dx
    pushf
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Transfer the drive no. into the dl register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     dl,     cl
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Make sure our edd_avaiable variable is zero by default.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     word ptr [_edd_available],      0
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Only check if we're booted from a hard disk.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    test    dl,     HEX (70)
    jnz     _no_edd
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Check if we have disk extensions.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ah,     HEX (41)
    mov     bx,     HEX (55AA)
    stc
    int     HEX (13)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; If the carry flag is set then disk extensions a unavailable.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    jc      _no_edd
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Bit 1 in the cx register should be set if disk extensions
    ;; are present.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    shr     cx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Test for carry (from shr) too.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    sbb     bx,     HEX (AA55) - 1
    jne     _no_edd
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Indicate that we have disk extensions.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     word ptr [_edd_available],      1

_no_edd:
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the ax, bx, cx and dx registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    popf
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the extra segment and si register as they
    ;; get clobbered by movsb.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    es
    push    si
    pushf
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Copy the BPB just above the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     di,     ss
    mov     es,     di
    
    mov     si,     11
    lea     di,     [bp - 64]
    
    mov     cx,     25
    rep     movsb
    
    cmp     byte ptr [HEX (01)],    HEX (58)
    jne     _not_fat32
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; If we get here then we're dealing with FAT32, so first
    ;; preserve the ax register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the volume id from the BPB.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     [HEX (43)]
    mov     word ptr [_signature + 0],      ax
    
    mov     ax,     [HEX (45)]
    mov     word ptr [_signature + 2],      ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the number of sectors per FAT.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     [HEX (24)]
    stosw
    
    mov     ax,     [HEX (26)]
    stosw
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the root cluster.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     [HEX (2C)]
    stosw
    
    mov     ax,     [HEX (2E)]
    stosw
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    si
    push    di
    push    ax
    push    dx

_calc_offsets:

    xor     ax,     ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate the offset of the first FAT.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     si,     [_hidden_sectors]
    mov     di,     [_hidden_sectors + 2]
    
    add     si,     [_reserved_sectors]
    adc     di,     0
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save the offset.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     [_fat_start],       si
    mov     [_fat_start + 2],       di
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate how many sectors both FATs require.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     al,     [_number_of_fats]
    cbw
    
    push    ax
    
    mul     word ptr [_sectors_per_fat32 + 2]
    add     di,     ax
    
    pop     ax
    mul     word ptr [_sectors_per_fat32]
    
    add     ax,     si
    adc     dx,     di
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save the offset.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     [_data_start],      ax
    mov     [_data_start + 2],      dx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     dx
    pop     ax
    pop     di
    pop     si
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate the sector mask.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     [_bytes_per_sector]
    shr     ax
    shr     ax
    dec     ax
    
    mov     [_fat_secmask],     ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate the sector shift.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     cx,     cx
    
    xchg    ax,     cx
    inc     cx

_secshift:

    inc     ax
    shr     cx
    
    cmp     cx,     1
    jne     _secshift
    
    mov     [_fat_secshift],        ax
    dec     cx
    
    mov     [_fat_sector],      cx
    mov     [_fat_sector + 2],      cx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the ax register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     ax

_after_copy:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the extra segment and si register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    popf
    pop     si
    pop     es
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the ax and dx registers (need as _next_cluster pops both).
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    dx
    push    ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the next sector.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    jmp     short   .L6

.L4:

    push    dx
    push    ax
    push    bx
    
    call    _convert_cluster32
    jc      .L7
    
    mov     cx,     bx
    pop     bx

.L5:

    call    _read_sectors

.L6:

    pop     ax
    pop     dx
    
    call    _nextcluster_fat32
    jmp     short   .L4

.L7:

    jmp     _all_read

;******************************************************************************
; @function        _not_fat32
;******************************************************************************
_not_fat32:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    di
    push    ax
    push    dx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the volume id from the BPB.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     [HEX (27)]
    mov     word ptr [_signature + 0],      ax
    
    mov     ax,     [HEX (29)]
    mov     word ptr [_signature + 2],      ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ....
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     ax,     ax
    
    mov     [_fat_sector],      ax
    mov     [_fat_sector + 2],      ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate the offset of the first FAT.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     si,     [_hidden_sectors]
    mov     di,     [_hidden_sectors + 2]
    
    add     si,     [_reserved_sectors]
    adc     di,     0
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save the offset ready for the fathelper at the start of kaishi.sys.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     [_fat_start],       si
    mov     [_fat_start + 2],       di
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate how many sectors both FATs require.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     al,     [_number_of_fats]
    cbw
    mul     word ptr [_sectors_per_fat]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Adjust the FAT offset to get the root directory offset.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     si,     ax
    adc     di,     dx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate how many sectors the root directory requires.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     HEX (0020)
    mul     word ptr [_root_entries]
    div     word ptr [_bytes_per_sector]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Adjust the root offset to get the data offset.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     si,     ax
    adc     di,     dx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save the offset for later use.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     [_data_start],      si
    mov     [_data_start + 2],      di
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     dx
    pop     ax
    pop     di
    
    popf
    pop     si
    pop     es
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Previded we've got a clean stack, the carry flag should be set from
    ;; the boot sector.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    jc      _all_read
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the address of the next cluster.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    call    _getfattype
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the ax and dx registers (need as _next_cluster pops both).
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    dx
    push    ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the next sector.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    jmp     short   .L1

.L2:

    push    dx
    push    ax
    push    bx
    
    call    [_convert_cluster]
    jc      _all_read
    
    mov     cx,     bx
    pop     bx

.L8:

    push    bp
    push    ax
    push    bx
    
    mov     ax,     HEX (0E2E)
    mov     bx,     HEX (0007)
    int     HEX (10)
    
    pop     bx
    pop     ax
    pop     bp
    call    _read_sectors

.L1:

    pop     ax
    pop     dx
    
    call    [_next_cluster]
    jmp     short   .L2

.L10:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Halt the system.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    hlt
    jmp     .L10

;******************************************************************************
; @function        _convert_cluster12
;******************************************************************************
_convert_cluster12:

    cmp     ax,     HEX (0FF8)
    jb      _convert_cluster12.c3
    
    stc
    retn

_convert_cluster12.c3:

    xor     dx,     dx
    
    sub     ax,     2
    sbb     dx,     0
    
    mov     bl,     [_sectors_per_cluster]
    dec     bx
    
    sub     bh,     bh
    inc     bx
    
    mul     bx
    
    add     ax,     [_data_start]
    add     dx,     [_data_start + 2]
    
    retn

;******************************************************************************
; @function        _convert_cluster16
;******************************************************************************
_convert_cluster16:

    cmp     ax,     HEX (FFF8)
    jb      _convert_cluster16.c3
    
    stc
    retn

_convert_cluster16.c3:

    xor     dx,     dx
    
    sub     ax,     2
    sbb     dx,     0
    
    mov     bl,     [_sectors_per_cluster]
    dec     bx
    
    sub     bh,     bh
    inc     bx
    
    mul     bx
    
    add     ax,     [_data_start]
    add     dx,     [_data_start + 2]
    
    retn

;******************************************************************************
; @function        _convert_cluster32
;******************************************************************************
_convert_cluster32:

    cmp     dx,     HEX (0FFF)
    jne     _convert_cluster32.c3
    
    cmp     ax,     HEX (FFF8)
    jb      _convert_cluster32.c3
    
    stc
    ret

_convert_cluster32.c3:

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
    
    add     ax,     [_data_start]
    adc     dx,     [_data_start + 2]
    
    ret

;******************************************************************************
; @function        _getfattype
;******************************************************************************
_getfattype:

    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    
    xor     ax,     ax

_getfattype.init:

    mov     al,     [_sectors_per_cluster]
    cbw
    mov     si,     ax
    
    mov     di,     [_bytes_per_sector]
    
    xor     dx,     dx
    xor     cx,     cx
    
    mov     ax,     [_total_sectors16]
    and     ax,     ax
    jnz     _getfattype.have_secs
    
    mov     ax,     [_total_sectors32]
    mov     dx,     [_total_sectors32 + 2]

_getfattype.have_secs:

    sub     ax,     [_reserved_sectors]
    sbb     dx,     0
    
    mov     cl,     [_number_of_fats]

_getfattype.sec_fat_loop:

    sub     ax,     [_sectors_per_fat]
    sbb     dx,     0
    loop    _getfattype.sec_fat_loop
    
    push    ax
    push    dx
    
    mov     ax,     [_root_entries]
    mov     bx,     32
    mul     bx
    
    add     ax,     di
    adc     dx,     0
    
    sub     ax,     1
    sbb     dx,     0
    
    div     di
    mov     bx,     ax
    
    pop     dx
    pop     ax
    
    sub     ax,     bx
    sbb     dx,     0
    
    div     si
    
    cmp     ax,     4096
    ja      _getfattype.fat16

_getfattype.fat12:

    mov     ax,     offset _convert_cluster12
    mov     [_convert_cluster],     ax
    
    mov     ax,     offset _nextcluster_fat12
    mov     [_next_cluster],        ax
    
    jmp     short   _getfattype.done

_getfattype.fat16:

    mov     ax,     offset _convert_cluster16
    mov     [_convert_cluster],     ax
    
    mov     ax,     offset _nextcluster_fat16
    mov     [_next_cluster],        ax

_getfattype.done:

    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    ret

;******************************************************************************
; @function        _nextcluster_fat12
;******************************************************************************
_nextcluster_fat12:

    push    cx
    push    si
    
    mov     si,     ax

_nextcluster_fat12.secoff:

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

_nextcluster_fat12.read_fat:

    push    es
    push    bx
    
    mov     bx,     ss
    mov     es,     bx
    xor     bx,     bx
    
    mov     ax,     [_fat_start]
    mov     dx,     [_fat_start + 2]
    
    add     ax,     si
    adc     dx,     0
    
    cmp     ax,     [_fat_sector]
    jne     _nextcluster_fat12.read
    
    cmp     dx,     [_fat_sector + 2]
    je      _nextcluster_fat12.cn_exit

_nextcluster_fat12.read:

    mov     [_fat_sector],      ax
    mov     [_fat_sector + 2],      dx
    
    mov     cx,     2
    call    _read_sectors

_nextcluster_fat12.cn_exit:

    pop     bx
    pop     es

_nextcluster_fat12.get_clust:

    pop     si
    popf
    pop     cx
    
    push    ds
    mov     ax,     ss
    mov     ds,     ax
    mov     ax,     [si]
    pop     ds
    jnc     _nextcluster_fat12.even_cluster
    
    mov     cl,     4
    shr     ax,     cl

_nextcluster_fat12.even_cluster:

    and     ax,     HEX (0FFF)

_nextcluster_fat12.compare:

    cmp     ax,     HEX (0FF8)
    cmc

_nextcluster_fat12.done:

    xor     dx,     dx
    
    pop     si
    pop     cx
    retn

;******************************************************************************
; @function        _nextcluster_fat16
;******************************************************************************
_nextcluster_fat16:

    push    cx
    push    si

_nextcluster_fat16.secoff:

    shl     ax
    
    push    cx
    pushf
    
    mov     cx,     [_bytes_per_sector]
    shl     cx
    
    xor     dx,     dx
    div     cx
    
    push    dx
    xchg    si,     ax

_nextcluster_fat16.read_fat:

    push    es
    push    bx
    
    mov     bx,     ss
    mov     es,     bx
    xor     bx,     bx
    
    mov     ax,     [_fat_start]
    mov     dx,     [_fat_start + 2]
    
    add     ax,     si
    adc     dx,     0
    
    cmp     ax,     [_fat_sector]
    jne     _nextcluster_fat16.read
    
    cmp     dx,     [_fat_sector + 2]
    je      _nextcluster_fat16.cn_exit

_nextcluster_fat16.read:

    mov     [_fat_sector],      ax
    mov     [_fat_sector + 2],      dx
    
    mov     cx,     2
    call    _read_sectors

_nextcluster_fat16.cn_exit:

    pop     bx
    pop     es

_nextcluster_fat16.get_clust:

    pop     si
    popf
    pop     cx
    
    mov     ax,     ss
    jnc     _nextcluster_fat16.first64
    
    add     ax,     HEX (1000)

_nextcluster_fat16.first64:

    push    ds
    mov     ds,     ax
    mov     ax,     [si]
    pop     ds

_nextcluster_fat16.compare:

    cmp     ax,     HEX (FFF8)
    cmc

_nextcluster_fat16.done:

    xor     dx,     dx
    
    pop     si
    pop     cx
    retn

;******************************************************************************
; @function        _nextcluster_fat32
;******************************************************************************
_nextcluster_fat32:

    push    es
    push    bx
    push    cx
    push    di
    
    mov     di,     ax
    and     di,     [_fat_secmask]
    
    mov     cx,     [_fat_secshift]

_nextcluster_fat32.cn_loop:

    shr     dx
    rcr     ax
    loop    _nextcluster_fat32.cn_loop
    
    shl     di
    shl     di
    
    add     ax,     [_fat_start]
    adc     dx,     [_fat_start + 2]
    
    mov     bx,     ss
    mov     es,     bx
    xor     bx,     bx
    
    cmp     ax,     [_fat_sector]
    jne     _nextcluster_fat32.read
    
    cmp     dx,     [_fat_sector + 2]
    je      _nextcluster_fat32.cn_exit

_nextcluster_fat32.read:

    mov     [_fat_sector],      ax
    mov     [_fat_sector + 2],      dx
    
    mov     cx,     1
    call    _read_sectors

_nextcluster_fat32.cn_exit:

    mov     ax,     es:[di]
    mov     dx,     es:[di + 2]
    
    cmp     dx,     HEX (0FFF)
    jne     _nextcluster_fat32.no_carry
    
    cmp     ax,     HEX (FFF8)
    jb      _nextcluster_fat32.no_carry
    
    pop     di
    pop     cx
    pop     bx
    pop     es
    
    stc
    ret

_nextcluster_fat32.no_carry:

    pop     di
    pop     cx
    pop     bx
    pop     es
    
    clc
    ret

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
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; If disk extensions aren't avaiable then we need to use CHS.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cmp     word ptr [_edd_available],       1
    jb      _read_sectors.chs

_read_sectors.lba:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; We'll create the LBA read on the stack.  8086 doesn't allow pushing
    ;; values directly so we'll use si.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     si,     si
    push    si
    push    si
    
    push    dx
    push    ax
    push    es
    push    bx
    
    mov     si,     1
    push    si
    
    mov     si,     16
    push    si
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; The dl register needs to be set to the drive number before we alter
    ;; the data segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     dl,     [_drive_no]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; We need to set si to sp as the LBA read requires ds:si.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     si,     sp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the data segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ds
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the ax and dx registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ax
    push    dx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the data segment to the stack segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ss
    pop     ds
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Read the sector into memory.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ah,     HEX (42)
    stc
    int     HEX (13)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the ax and dx registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     dx
    pop     ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the data segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     ds
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Cleanup the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     sp,     16
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; If there was no carry the we successfully read the sector.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    jnc     _read_sectors.success

_read_sectors.chs:

    div     word ptr [_sectors_per_track]
            ; ax = LBA / SPT
            ; dx = LBA # SPT        = sector - 1
    
    mov     cx,     dx
    inc     cx
            ; cx = sector no.
    
    xor     dx,     dx
    div     word ptr [_heads_per_cylinder]
            ; ax = (LBA / SPT) / HPC = cylinder
            ; dx = (LBA / SPT) # HPC = head
    
    mov     ch,     al
            ; ch = LSB 0...7 of cylinder no.
    ror     ah
    ror     ah
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
    jnc     _read_sectors.success

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
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Invoke the BIOS.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    int     HEX (18)

_read_sectors.success:

    add     bx,     [_bytes_per_sector]
    jnc     _read_sectors.no_incr
    
    mov     cx,     es
    add     ch,     16
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
;; Make sure we don't exceed 1024 bytes.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
db      1024 - (. - _start)     dup (0)

;******************************************************************************
; @function         _all_read
;******************************************************************************
_all_read:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Clear the screen.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     HEX (0600)
    mov     bh,     HEX (07)
    xor     cx,     cx
    mov     dx,     HEX (184F)
    int     HEX (10)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Reset the cursor.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ah,     HEX (02)
    xor     bh,     bh
    xor     dx,     dx
    int     HEX (10)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Clean up the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     bx
    pop     ax
    pop     dx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Convert the value in the bx register to a segment and add
    ;; it to the extra segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     cl,     4
    shr     bx,     cl
    
    mov     cx,     es
    add     cx,     bx
    mov     es,     cx

%ifdef  __AMD64__

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; CPUID was introduced with the Intel 486 so to use that instruction we
;; need to tell the assembler that we'll be using that CPU.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
.486

;******************************************************************************
; @function         _check
;******************************************************************************
_check:

    movzx   ebp,    bp
    movzx   esp,    sp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the data segment with the code segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     cs
    mov     ds,     ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Make sure we have an compatible CPU.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    call    _has_cpuid
    jc      _check_long_mode
    
    call    _error
    db      "Freeldr requires a processor with CPUID",      HEX (0D),   HEX (0A),   HEX (00)

;******************************************************************************
; @function        _check_long_mode
;******************************************************************************
_check_long_mode:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Is the processor info function supported?
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    HEX (80000000)                                              ; Get Highest Extended Function Supported
    cpuid
    
    cmp     eax,    HEX (80000001)
    jb      _no_long_mode
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Use processor info function to see if 64-bit mode is supported.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    HEX (80000001)                                              ; Extended 64-bit mode is supported
    cpuid
    
    test    edx,    (1 << 29)                                                   ; 64-bit mode bit
    jnz     _cpu_ok

;******************************************************************************
; @function        _no_pae
;******************************************************************************
_no_long_mode:

    call    _error
    db      "Freeldr requires a AMD64 compatible CPU",      HEX (0D),   HEX (0A),   HEX (00)

%else

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; From this point onwards we want to be able to use Intel 386 instructions.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
.386

;******************************************************************************
; @function         _check
;******************************************************************************
_check:

    movzx   ebp,    bp
    movzx   esp,    sp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the data segment with the code segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     cs
    mov     ds,     ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Make sure we have an i386 or newer CPU.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    call    _query_cpu
    
    cmp     ax,     3
    je      _cpu_ok
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Print an error.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    call    _error
    db      "Freeldr requires a i386 or newer CPU",     HEX (0D),   HEX (0A),   HEX (00)

%endif

;******************************************************************************
; @function        _cpu_ok
;******************************************************************************
_cpu_ok:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Check what type of executable the actual loader is.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edi,    offset __etext
    add     edi,    offset __edata
    add     edi,    offset __end
    
    cmp     byte ptr [edi + 0],     'A'
    jne     _fail
    
    cmp     byte ptr [edi + 1],     'X'
    jne     _fail
    
    cmp     byte ptr [edi + 2],     'E'
    jne     _fail
    
%ifdef  __AMD64__

    cmp     byte ptr [edi + 3],     62
    je      _relocate

%else

    cmp     byte ptr [edi + 3],     3
    je      _relocate

%endif

;******************************************************************************
; @function        _fail
;******************************************************************************
_fail:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Print an error.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    call    _error
    db      "Unsupported executable format",        HEX (0D),   HEX (0A),   HEX (00)

%ifdef  __AMD64__

;******************************************************************************
; @function        _relocate
;******************************************************************************
_relocate:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the drive number and push it to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    movzx   edx,    byte ptr [_drive_no]
    push    edx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save the value in es to fs.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    es
    mov     fs,     eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Zero out the extra segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     eax,    eax
    mov     es,     eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;-
    ;; Move the value in edi to eax.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;-
    mov     eax,    edi
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Convert the code segment into a byte offset.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ebx,    cs
    shl     ebx,    4
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Add the value in eax to the value in ecx.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     ebx,    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset 20 should be the entry address so load the value into eax.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    ebx
    add     eax,    40
    call    _read_linear_dword
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Add the size of the aout header.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     eax,    64
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset the value in eax the value in the ecx register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     eax,    ebx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ...
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ebx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Create the call on the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edx,    eax
    
    and     eax,    HEX (0F)
    shr     edx,    4
    
    push    dx
    push    ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Check if we have relocations.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    ebx
    add     eax,    48
    call    _read_linear_dword
    
    mov     ecx,    eax
    
    and     ecx,    ecx
    jz      _execute
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ...
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    dword ptr 48
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate the offset of the relocation table.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    ebx
    add     eax,    8
    call    _read_linear_dword
    
    mov     ecx,    eax
    
    mov     eax,    ebx
    add     eax,    16
    call    _read_linear_dword
    
    add     ecx,    eax
    add     ecx,    64
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ecx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset the value in eax by the value in the ebx register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    ebx
    add     eax,    64
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; We want to offset from the header size.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    64
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Do the relocations.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    call    _do_relocations
    add     esp,    16
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Check if we have relocations.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    ebx
    add     eax,    56
    call    _read_linear_dword
    
    mov     ecx,    eax
    
    and     ecx,    ecx
    jz      _execute
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ...
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    dword ptr 56
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate the offset of the relocation table.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    ebx
    add     eax,    8
    call    _read_linear_dword
    
    mov     ecx,    eax
    
    mov     eax,    ebx
    add     eax,    16
    call    _read_linear_dword
    
    add     ecx,    eax
    
    mov     eax,    ebx
    add     eax,    48
    call    _read_linear_dword
    
    add     ecx,    eax
    add     ecx,    64
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ecx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset the value in eax by the value in the ebx register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    ebx
    add     eax,    64
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; We want to offset from the header size.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edi,    64
    
    mov     eax,    ebx
    add     eax,    8
    call    _read_linear_dword
    
    add     edi,    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    edi
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Do the relocations.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    call    _do_relocations
    add     esp,    16

;******************************************************************************
; @function        _execute
;******************************************************************************
_execute:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the edi with the value in ebx.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edi,    ebx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Add the size of the aout header.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     edi,    64
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the size of the text/code section.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     esi,    ebx
    add     esi,    8
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset edi by the value.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     edi,    es:[esi]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the size of the data section.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     esi,    ebx
    add     esi,    16
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset edi by the value.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     edi,    es:[esi]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the BSS size.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     esi,    ebx
    add     esi,    24
    mov     ecx,    es:[esi]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ,..
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    edi
    push    ecx
    push    es
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; EDI is the linear BSS address.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    edi
    mov     edx,    eax
    
    and     eax,    HEX (0F)
    shr     edx,    4
    
    mov     es,     dx
    mov     di,     ax
    
    xor     al,     al
    rep     stosb
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ,..
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     es
    pop     ecx
    pop     edi
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Far jump to the real start.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    retf

;******************************************************************************
; @function         _do_relocations
;******************************************************************************
_do_relocations:

    push    ebp
    
    mov     ebp,    esp
    sub     esp,    16
    
    mov     eax,    ebx
    
    add     eax,    dword ptr [ebp + 18]
    call    _read_linear_dword
    
    xor     edx,    edx
    mov     ecx,    16
    div     ecx
    
    mov     ecx,    eax
    xor     edi,    edi

_do_relocations.loop:

    mov     esi,    dword ptr [ebp + 14]
    add     esi,    edi
    
    mov     eax,    ebx
    add     eax,    esi
    call    _read_linear_dword
    
    mov     dword ptr [ebp - 12],   eax
    
    mov     edx,    eax
    
    mov     eax,    ebx
    
    add     eax,    esi
    add     eax,    8
    
    call    _read_linear_dword
    mov     dword ptr [ebp - 8],    eax
    
    mov     eax,    ebx
    add     eax,    esi
    add     eax,    12
    call    _read_linear_dword
    
    shr     eax,    24
    mov     dword ptr [ebp - 4],    eax
    
    mov     edx,    dword ptr [ebp - 4]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; low encoded size bits are:
    ;;   0x02 = word
    ;;   0x04 = dword
    ;;   0x06 = qword
    ;; bit 0x08 = PC-relative
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    and     dl,     0x06
    
    cmp     dl,     0x06
    je      _do_relocations.qword
    
    cmp     dl,     0x04
    je      _do_relocations.dword
    
    cmp     dl,     0x02
    je      _do_relocations.word
    
    jmp     _do_relocations.next

_do_relocations.word:

    push    edi
    
    mov     edi,    dword ptr [ebp + 6]
    add     edi,    dword ptr [ebp - 12]
    
    mov     eax,    dword ptr [ebp + 10]
    
    mov     edx,    ebx
    add     edx,    edi
    
    call    _add_linear_word
    pop     edi
    
    jmp     _do_relocations.next

_do_relocations.dword:

    push    edi
    
    mov     edi,    dword ptr [ebp + 6]
    add     edi,    dword ptr [ebp - 12]
    
    mov     eax,    dword ptr [ebp + 10]
    
    mov     edx,    ebx
    add     edx,    edi
    
    call    _add_linear_dword
    pop     edi
    
    jmp     _do_relocations.next

_do_relocations.qword:

    push    edi
    
    mov     edi,    dword ptr [ebp + 6]
    add     edi,    dword ptr [ebp - 12]
    
    mov     eax,    dword ptr [ebp + 10]
    
    mov     edx,    ebx
    add     edx,    edi
    
    call    _add_linear_qword
    pop     edi

_do_relocations.next:

    add     edi,    16
    
    dec     ecx
    jnz     _do_relocations.loop

_do_relocations.done:

    add     esp,    16
    pop     ebp
    
    ret

;******************************************************************************
; @function        _read_linear_dword
;******************************************************************************
_read_linear_dword:

    push    edx
    push    ebx
    push    es
    
    mov     edx,    eax
    
    and     eax,    HEX (0F)
    shr     edx,    4
    
    mov     es,     dx
    mov     bx,     ax
    
    mov     eax,    es:[bx]
    
    pop     es
    pop     ebx
    pop     edx
    ret

;******************************************************************************
; @function        _add_linear_word
;******************************************************************************
_add_linear_word:

    push    ebx
    push    es
    
    mov     ebx,    edx
    
    and     edx,    HEX (0F)
    shr     ebx,    4
    
    mov     es,     bx
    mov     bx,     dx
    
    add     ax,     es:[bx]
    mov     es:[bx],   ax
    
    pop     es
    pop     ebx
    ret

;******************************************************************************
; @function        _add_linear_dword
;******************************************************************************
_add_linear_dword:

    push    ebx
    push    es
    
    mov     ebx,    edx
    
    and     edx,    HEX (0F)
    shr     ebx,    4
    
    mov     es,     bx
    mov     bx,     dx
    
    add     eax,    es:[bx]
    mov     es:[bx],   eax
    
    pop     es
    pop     ebx
    ret

;******************************************************************************
; @function        _add_linear_qword
;******************************************************************************
_add_linear_qword:

    push    ebx
    push    es
    
    mov     ebx,    edx
    
    and     edx,    HEX (0F)
    shr     ebx,    4
    
    mov     es,     bx
    mov     bx,     dx
    
    add     dword ptr es:[bx + 0], eax
    adc     dword ptr es:[bx + 4], 0
    
    pop     es
    pop     ebx
    ret

;******************************************************************************
; @function        _has_cpuid
;******************************************************************************
_has_cpuid:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    eax
    push    ecx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Copy flags to eax and ecx.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pushfd
    pop     eax
    
    mov     ecx,    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Set flag 21 (the ID bit).
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     eax,    (1 << 21)
    
    push    eax
    popfd
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Copy flags back to eax.  If CPUID is supported,
    ;; bit 21 will still be set.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pushfd
    pop     eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the original flags from ecx.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ecx
    popfd
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the return flag (carry) to unsupported.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    clc
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; If eax and ecx are equal, then flag 21 didn't remain set, and CPUID is
    ;; not supported.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     eax,    ecx
    jz      _has_cpuid.done                                       ; CPUID is not supported

_has_cpuid.supported:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; CPUID is supported.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    stc

_has_cpuid.done:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     ecx
    pop     eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return to caller.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ret

%else

;******************************************************************************
; @function        _relocate
;******************************************************************************
_relocate:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the drive number and push it to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    movzx   edx,    byte ptr [_drive_no]
    push    edx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Zero out the extra segment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     eax,    eax
    mov     es,     eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;-
    ;; Move the value in edi to eax.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;-
    mov     eax,    edi
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Convert the code segment into a byte offset.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ebx,    cs
    shl     ebx,    4
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Add the value in eax to the value in ecx.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     ebx,    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset 20 should be the entry address so load the value into eax.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edi,    ebx
    add     edi,    20
    mov     eax,    es:[edi]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Add the size of the aout header.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     eax,    32
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset the value in eax the value in the ecx register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     eax,    ebx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ...
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ebx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Create the call on the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edx,    eax
    
    and     eax,    HEX (0F)
    shr     edx,    4
    
    push    dx
    push    ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Check if we have relocations.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edi,    ebx
    add     edi,    24
    mov     ecx,    es:[edi]
    
    and     ecx,    ecx
    jz      _execute
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ...
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    dword ptr 24
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate the offset of the relocation table.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edi,    ebx
    add     edi,    4
    mov     ecx,    es:[edi]
    
    mov     edi,    ebx
    add     edi,    8
    add     ecx,    es:[edi]
    
    add     ecx,    32
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ecx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset the value in eax by the value in the ebx register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    ebx
    add     eax,    32
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; We want to offset from the header size.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    32
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Do the relocations.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    call    _do_relocations
    add     esp,    16
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Check if we have relocations.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edi,    ebx
    add     edi,    28
    mov     ecx,    es:[edi]
    
    and     ecx,    ecx
    jz      _execute
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ...
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    dword ptr 28
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Calculate the offset of the relocation table.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edi,    ebx
    add     edi,    4
    mov     ecx,    es:[edi]
    
    mov     edi,    ebx
    add     edi,    8
    add     ecx,    es:[edi]
    
    mov     edi,    ebx
    add     edi,    24
    add     ecx,    es:[edi]
    
    add     ecx,    32
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ecx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset the value in eax by the value in the ebx register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    ebx
    add     eax,    32
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; We want to offset from the header size.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    32
    
    mov     edi,    ebx
    add     edi,    4
    add     eax,    es:[edi]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Push the value to the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Do the relocations.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    call    _do_relocations
    add     esp,    16

;******************************************************************************
; @function        _execute
;******************************************************************************
_execute:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the edi with the value in ebx.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edi,    ebx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Add the size of the aout header.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     edi,    32
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the size of the text/code section.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     esi,    ebx
    add     esi,    4
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset edi by the value.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     edi,    es:[esi]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the size of the data section.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     esi,    ebx
    add     esi,    8
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Offset edi by the value.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     edi,    es:[esi]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the BSS size.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     esi,    ebx
    add     esi,    12
    mov     ecx,    es:[esi]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ,..
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    edi
    push    ecx
    push    es
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; EDI is the linear BSS address.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    edi
    mov     edx,    eax
    
    and     eax,    HEX (0F)
    shr     edx,    4
    
    mov     es,     dx
    mov     di,     ax
    
    xor     al,     al
    rep     stosb
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; ,..
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     es
    pop     ecx
    pop     edi
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Far jump to the real start.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    retf

;******************************************************************************
; @function         _do_relocations
;******************************************************************************
_do_relocations:

    push    ebp
    
    mov     ebp,    esp
    sub     esp,    8
    
    mov     eax,    ebx
    add     eax,    dword ptr [ebp + 18]
    
    call    _read_linear_dword
    
    xor     edx,    edx
    mov     ecx,    8
    div     ecx
    
    mov     ecx,    eax
    xor     edi,    edi

_do_relocations.loop:

    mov     esi,    dword ptr [ebp + 14]
    add     esi,    edi
    
    mov     eax,    ebx
    add     eax,    esi
    
    call    _read_linear_dword
    mov     dword ptr [ebp - 8],    eax
    
    mov     eax,    ebx
    
    add     eax,    esi
    add     eax,    4
    
    call    _read_linear_dword
    
    mov     edx,    eax
    shr     edx,    24
    
    mov     eax,    dword ptr [ebp - 8]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; low encoded size bits are:
    ;;   0x02 = word
    ;;   0x04 = dword
    ;;   0x06 = qword
    ;; bit 0x08 = PC-relative
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    and     dl,     0x06
    
    cmp     dl,     0x06
    je      _do_relocations.qword
    
    cmp     dl,     0x04
    je      _do_relocations.dword
    
    cmp     dl,     0x02
    je      _do_relocations.word
    
    jmp     _do_relocations.next

_do_relocations.word:

    push    edi
    
    mov     edi,    dword ptr [ebp + 6]
    add     edi,    eax
    
    mov     eax,    dword ptr [ebp + 10]
    
    mov     edx,    ebx
    add     edx,    edi
    
    call    _add_linear_word
    pop     edi
    
    jmp     _do_relocations.next

_do_relocations.dword:

    push    edi
    
    mov     edi,    dword ptr [ebp + 6]
    add     edi,    eax
    
    mov     eax,    dword ptr [ebp + 10]
    
    mov     edx,    ebx
    add     edx,    edi
    
    call    _add_linear_dword
    
    pop     edi
    jmp     _do_relocations.next

_do_relocations.qword:

    push    edi
    
    mov     edi,    dword ptr [ebp + 6]
    add     edi,    eax
    
    mov     eax,    dword ptr [ebp + 10]
    
    mov     edx,    ebx
    add     edx,    edi
    
    call    _add_linear_qword
    pop     edi

_do_relocations.next:

    add     edi,    8
    
    dec     ecx
    jnz     _do_relocations.loop

_do_relocations.done:

    add     esp,    8
    pop     ebp
    
    ret

;******************************************************************************
; @function        _read_linear_dword
;******************************************************************************
_read_linear_dword:

    push    edx
    push    ebx
    push    es
    
    mov     edx,    eax
    
    and     eax,    HEX (0F)
    shr     edx,    4
    
    mov     es,     dx
    mov     bx,     ax
    
    mov     eax,    es:[bx]
    
    pop     es
    pop     ebx
    pop     edx
    ret

;******************************************************************************
; @function        _add_linear_word
;******************************************************************************
_add_linear_word:

    push    ebx
    push    es
    
    mov     ebx,    edx
    
    and     edx,    HEX (0F)
    shr     ebx,    4
    
    mov     es,     bx
    mov     bx,     dx
    
    add     ax,     es:[bx]
    mov     es:[bx],   ax
    
    pop     es
    pop     ebx
    ret

;******************************************************************************
; @function        _add_linear_dword
;******************************************************************************
_add_linear_dword:

    push    ebx
    push    es
    
    mov     ebx,    edx
    
    and     edx,    HEX (0F)
    shr     ebx,    4
    
    mov     es,     bx
    mov     bx,     dx
    
    add     eax,    es:[bx]
    mov     es:[bx],   eax
    
    pop     es
    pop     ebx
    ret

;******************************************************************************
; @function        _add_linear_qword
;******************************************************************************
_add_linear_qword:

    push    ebx
    push    es
    
    mov     ebx,    edx
    
    and     edx,    HEX (0F)
    shr     ebx,    4
    
    mov     es,     bx
    mov     bx,     dx
    
    add     dword ptr es:[bx + 0], eax
    adc     dword ptr es:[bx + 4], 0
    
    pop     es
    pop     ebx
    ret

;******************************************************************************
; @function        _query_cpu
;******************************************************************************
_query_cpu:

    push    bx
    push    cx
    pushf

_query_cpu.begin:

    xor     bx,     bx
    
    push    bx
    popf
    
    pushf
    pop     ax
    
    and     ax,     HEX (F000)
    
    cmp     ax,     HEX (F000)
    jnz     _query_cpu.is_286
    
    mov     ax,     sp
    xor     cx,     cx
    
    push    cx
    inc     cx
    
    db      HEX (8F)
    db      HEX (C1)
    
    mov     sp,     ax
    
    or      cx,     cx
    jz      _query_cpu.is_808x
    
    mov     bx,     cx
    jmp     _query_cpu.done

_query_cpu.is_808x:

    mov     ax,     1
    
    mov     cl,     64
    shr     ax,     cl
    
    or      ax,     ax
    jz      _query_cpu.is_086
    
    mov     bx,     1

_query_cpu.is_086:

    jmp     _query_cpu.done

_query_cpu.is_286:

    mov     ax,     HEX (F000)
    mov     bx,     2
    
    push    ax
    popf
    
    pushf
    pop     ax
    
    test    ax,     HEX (F000)
    jz      _query_cpu.done
    
    mov     bx,     3

_query_cpu.done:

    mov     ax,     bx
    popf
    
    pop     cx
    pop     bx
    
    ret

%endif

;******************************************************************************
; @function         _error
;******************************************************************************
_error:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Disable interrupts.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cli
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the address of the string from the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     si

_error.loop:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get a byte from ds:si.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    lodsb
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; If we've hit a zero then we're done.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    and     al,     al
    jz      _error.wait
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ax
    push    bx
    push    bp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Print the character.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ah,     HEX (0E)
    mov     bx,     HEX (0007)
    int     HEX (10)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     bp
    pop     bx
    pop     ax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Loop again.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    jmp     _error.loop

_error.wait:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Wait for a keypress.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     ax,     ax
    int     HEX (16)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Stop floppy motor.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     dx,     HEX (03F2)
    xor     al,     al
    out     dx,     al
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Reset console.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ax,     HEX (0003)
    int     HEX (10)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Cold reboot.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     ax,     ax
    mov     es,     ax
    
    mov     bx,     HEX (0472)
    mov     word ptr es:[bx],   0
    
    jmp     HEX (F000) : HEX (FFF0)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Make sure we don't exceed 2048 bytes.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
db      2048 - (. - _start)     dup (0)

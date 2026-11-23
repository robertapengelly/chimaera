;******************************************************************************
; @file             time.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function         _bcd2int
;******************************************************************************
_bcd2int:

    push    ebp
    mov     ebp,    esp
    
    push    ebx
    push    ecx
    
    xor     ebx,    ebx
    xor     ecx,    ecx
    
    mov     eax,    dword ptr [ebp + 8]
    mov     ebx,    dword ptr [ebp + 8]
    
    and     ax,     HEX (0F)
    mov     ecx,    eax
    
    shr     bx,     4
    and     bx,     HEX (0F)
    
    mov     eax,    10
    mul     ebx
    
    add     eax,    ecx
    
    pop     ecx
    pop     ebx
    pop     ebp
    ret

;******************************************************************************
; @function         _cmos_read
;******************************************************************************
_cmos_read:

    push    ebp
    
    mov     ebp,    esp
    sub     esp,    4
    
    push    ecx
    push    edx
    
    xor     eax,    eax
    
    mov     dx,     HEX (70)
    in      al,     dx
    
    and     al,     1 << 7
    
    mov     ecx,    dword ptr [ebp + 8]
    and     cl,     HEX (7F)
    
    or      al,     cl
    out     dx,     al
    
    mov     dx,     HEX (71)
    in      al,     dx
    
    pop     edx
    pop     ecx
    
    add     esp,    4
    pop     ebp
    
    ret

;******************************************************************************
; @function         _cmos_time
;******************************************************************************
_cmos_time:

    push    ebp
    mov     ebp,    esp
    
    push    ebx
    push    ecx
    push    esi
    
    mov     esi,    dword ptr [ebp + 8]
    xor     ebx,    ebx
    
    mov     ecx,    3

.L3:

    push    ebx
    
    call    _cmos_read
    add     esp,    4
    
    push    eax
    
    call    _bcd2int
    add     esp,    4
    
    mov     byte ptr [esi],     al
    inc     esi
    
    add     ebx,    2
    
    dec     ecx
    jnz     .L3
    
    mov     ebx,    7
    mov     ecx,    3

.L2:

    push    ebx
    
    call    _cmos_read
    add     esp,    4
    
    push    eax
    
    call    _bcd2int
    add     esp,    4
    
    mov     byte ptr [esi],     al
    inc     esi
    
    inc     ebx
    
    dec     ecx
    jnz     .L2

.L1:

    push    dword ptr 50
    
    call    _cmos_read
    add     esp,    4
    
    push    eax
    
    call    _bcd2int
    add     esp,    4
    
    mov     byte ptr [esi],     al
    
    pop     esi
    pop     ecx
    pop     ebx
    pop     ebp
    ret

;******************************************************************************
; @function         _year_to_gdn
;******************************************************************************
_year_to_gdn:

    push    ebp
    
    mov     ebp,    esp
    sub     esp,    32
    
    push    ebx
    push    ecx
    
    dec     dword ptr [ebp + 8]
    
    mov     ecx,    dword ptr [ebp + 8]
    mov     edx,    1374389535
    
    mov     eax,    ecx
    imul    edx
    
    mov     eax,    edx
    
    sar     eax,    7
    sar     ecx,    31
    
    mov     edx,    ecx
    sub     eax,    edx
    mov     dword ptr [ebp - 24],   eax
    
    mov     ecx,    dword ptr [ebp + 8]
    
    mov     edx,    1374389535
    mov     eax,    ecx
    
    imul    edx
    
    mov     eax,    edx
    sar     eax,    7
    
    mov     edx,    ecx
    sar     edx,    31
    sub     eax,    edx
    
    imul    edx,    eax,    400
    
    mov     eax,    ecx
    sub     eax,    edx
    mov     dword ptr [ebp - 20],   eax
    
    mov     ecx,    dword ptr [ebp - 20]
    mov     edx,    1374389535
    
    mov     eax,    ecx
    imul    edx
    
    mov     eax,    edx
    
    sar     eax,    5
    sar     ecx,    31
    
    mov     edx,    ecx
    sub     eax,    edx
    
    mov     dword ptr [ebp - 16],   eax
    mov     ecx,    dword ptr [ebp - 20]
    
    mov     edx,    1374389535
    mov     eax,    ecx
    
    imul    edx
    
    mov     eax,    edx
    sar     eax,    5
    
    mov     edx,    ecx
    sar     edx,    31
    sub     eax,    edx
    
    imul    edx,    eax,    100
    
    mov     eax,    ecx
    sub     eax,    edx
    mov     dword ptr [ebp - 12],   eax
    
    mov     eax,    dword ptr [ebp - 12]
    lea     edx,    [eax + 3]
    
    test    eax,    eax
    jns     .L8
    
    mov     eax,    edx

.L8:

    sar     eax,    2
    mov     dword ptr [ebp - 8],    eax
    
    mov     eax,    dword ptr [ebp - 12]
    cdq
    shr     edx,    30
    
    add     eax,    edx
    
    and     eax,    3
    sub     eax,    edx
    
    mov     dword ptr [ebp - 4],    eax
    
    cmp     dword ptr [ebp + 12],   0
    je      .L4
    
    cmp     dword ptr [ebp - 4],    3
    jne     .L6
    
    cmp     dword ptr [ebp - 12],   99
    jne     .L7
    
    cmp     dword ptr [ebp - 20],   399
    jne     .L6

.L7:

    mov     edx,    1
    jmp     .L5

.L6:

    xor     edx,    edx

.L5:

    mov     eax,    dword ptr [ebp + 12]
    mov     dword ptr [eax],    edx

.L4:

    mov     eax,    dword ptr [ebp - 24]
    imul    edx,    eax,    146097
    
    mov     eax,    dword ptr [ebp - 16]
    imul    eax,    eax,    36524
    
    add     edx,    eax
    
    mov     eax,    dword ptr [ebp - 8]
    imul    eax,    eax,    1461
    
    add     edx,    eax
    
    mov     eax,    dword ptr [ebp - 4]
    imul    eax,    eax,    365
    
    add     eax,    edx
    
    pop     ecx
    pop     ebx
    
    add     esp,    32
    pop     ebp
    
    ret

_month_to_gdn:                  dw      0,      31,     59,     90,     120,    151,    181,    212,    243,    273,    304,    334

;******************************************************************************
; @function         _epoch
;******************************************************************************
_epoch:

    push    ebp
    mov     ebp,    esp
    
    mov     eax,    dword ptr [ebp + 8]
    imul    edx,    eax,    86400
    
    mov     eax,    dword ptr [ebp + 12]
    imul    eax,    eax,    3600
    
    add     edx,    eax
    
    mov     eax,    dword ptr [ebp + 16]
    imul    eax,    eax,    60
    
    add     edx,    eax
    
    mov     eax,    dword ptr [ebp + 20]
    add     eax,    edx
    sub     eax,    2006054656
    
    pop     ebp
    ret


;******************************************************************************
; @function         _time
;******************************************************************************
global      _time
_time:

    push    ebp
    
    mov     ebp,    esp
    sub     esp,    12
    
    push    ebx
    push    ecx
    
    lea     eax,    [ebp - 8]
    push    eax
    
    call    _cmos_time
    add     esp,    4
    
    movzx   ebx,    byte ptr [ebp - 3]
    
    movzx   eax,    byte ptr [ebp - 2]
    xor     edx,    edx
    
    mov     ecx,    100
    mul     ecx
    
    add     eax,    ebx
    
    lea     ebx,    [ebp - 12]
    push    ebx
    
    push    eax
    
    call    _year_to_gdn
    add     esp,    8
    
    movzx   esi,    byte ptr [ebp - 4]
    
    dec     esi
    shl     esi
    
    add     esi,    offset _month_to_gdn
    
    movzx   ecx,    word ptr [esi]
    add     eax,    ecx
    
    cmp     dword ptr [ebp - 12],       0
    je      .L9
    
    cmp     byte ptr [ebp - 4],         2
    jbe     .L9
    
    inc     eax

.L9:

    movzx   ecx,    byte ptr [ebp - 5]
    dec     ecx
    
    add     eax,    ecx
    
    movzx   ecx,    byte ptr [ebp - 8]
    push    ecx
    
    movzx   ecx,    byte ptr [ebp - 7]
    push    ecx
    
    movzx   ecx,    byte ptr [ebp - 6]
    push    ecx
    
    push    eax
    
    call    _epoch
    add     esp,    16
    
    pop     ecx
    pop     ebx
    
    add     esp,    12
    pop     ebp
    
    ret

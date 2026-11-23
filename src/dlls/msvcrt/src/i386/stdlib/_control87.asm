;******************************************************************************
; @file             _control87.asm
;******************************************************************************
.option     segment:use32
.387

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function        __control87
;******************************************************************************
global      __control87
__control87:

    push    ebp
    
    mov     ebp,    esp
    sub     esp,    4
    
    fnstcw  word ptr [ebp - 4]
    
    movzx   eax,    word ptr [ebp - 4]
    xor     ecx,    ecx
    
    test    eax,    HEX (0001)
    jz      .no_invalid
    
    or      ecx,    HEX (00000010)

.no_invalid:

    test    eax,    HEX (0002)
    jz      .no_denormal
    
    or      ecx,    HEX (00080000)

.no_denormal:

    test    eax,    HEX (0004)
    jz      .no_zerodivide
    
    or      ecx,    HEX (00000008)

.no_zerodivide:

    test    eax,    HEX (0008)
    jz      .no_overflow
    
    or      ecx,    HEX (00000004)

.no_overflow:

    test    eax,    HEX (0010)
    jz      .no_underflow
    
    or      ecx,    HEX (00000002)

.no_underflow:

    test    eax,    HEX (0020)
    jz      .no_inexact
    
    or      ecx,    HEX (00000001)

.no_inexact:

    mov     edx,    eax
    
    shr     edx,    2
    and     edx,    HEX (00000300)
    
    or      ecx,    edx
    
    mov     edx,    eax
    and     edx,    HEX (00000300)
    
    cmp     edx,    0
    jne     .not_pc24
    
    or      ecx,    HEX (00020000)

.not_pc24:

    cmp     edx,    HEX (00000200)
    jne     .not_pc53
    
    or      ecx,    HEX (00010000)

.not_pc53:

    test    eax,    HEX (00001000)
    jz      .no_affine
    
    or      ecx,    HEX (00040000)

.no_affine:

    mov     edx,    dword ptr [ebp + 12]
    and     edx,    HEX (000F031F)
    
    mov     eax,    edx
    not     eax
    
    and     ecx,    eax
    and     edx,    dword ptr [ebp + 8]
    
    or      ecx,    edx
    
    movzx   eax,    word ptr [ebp - 4]
    and     eax,    HEX (0000E0C0)
    
    test    ecx,    HEX (00000010)
    jz      .set_no_invalid
    
    or      eax,    HEX (0001)

.set_no_invalid:

    test    ecx,    HEX (00080000)
    jz      .set_no_denormal
    
    or      eax,    HEX (0002)

.set_no_denormal:

    test    ecx,    HEX (00000008)
    jz      .set_no_zerodivide
    
    or      eax,    HEX (0004)

.set_no_zerodivide:

    test    ecx,    HEX (00000004)
    jz      .set_no_overflow
    
    or      eax,    HEX (0008)

.set_no_overflow:

    test    ecx,    HEX (00000002)
    jz      .set_no_underflow
    
    or      eax,    HEX (0010)

.set_no_underflow:

    test    ecx,    HEX (00000001)
    jz      .set_no_inexact
    
    or      eax,    HEX (0020)

.set_no_inexact:

    mov     edx,    ecx
    and     edx,    HEX (00000300)
    
    shl     edx,    2
    or      eax,    edx
    
    mov     edx,    ecx
    and     edx,    HEX (00030000)
    
    cmp     edx,    HEX (00020000)
    je      .set_precision_done
    
    cmp     edx,    HEX (00010000)
    jne     .set_precision64
    
    or      eax,    HEX (0200)
    jmp     .set_precision_done

.set_precision64:

    or      eax,    HEX (0300)

.set_precision_done:

    test    ecx,    HEX (00040000)
    jz      .set_no_affine
    
    or      eax,    HEX (1000)

.set_no_affine:

    mov     word ptr [ebp - 4],    ax
    fldcw   word ptr [ebp - 4]
    
    mov     eax,    ecx
    mov     esp,    ebp
    pop     ebp
    ret

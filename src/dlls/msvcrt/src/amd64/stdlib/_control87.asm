;******************************************************************************
; @file             _control87.asm
;******************************************************************************
%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function        _control87
;******************************************************************************
global      _control87
_control87:

    sub     rsp,    16
    
    stmxcsr dword ptr [rsp]
    mov     eax,    dword ptr [rsp]
    
    xor     r8d,    r8d

    test    eax,    HEX (00000080)
    jz      .no_invalid
    
    or      r8d,    HEX (00000010)

.no_invalid:

    test    eax,    HEX (00000100)
    jz      .no_denormal
    
    or      r8d,    HEX (00080000)

.no_denormal:

    test    eax,    HEX (00000200)
    jz      .no_zerodivide
    
    or      r8d,    HEX (00000008)

.no_zerodivide:

    test    eax,    HEX (00000400)
    jz      .no_overflow
    
    or      r8d,    HEX (00000004)

.no_overflow:

    test    eax,    HEX (00000800)
    jz      .no_underflow
    
    or      r8d,    HEX (00000002)

.no_underflow:

    test    eax,    HEX (00001000)
    jz      .no_inexact
    
    or      r8d,    HEX (00000001)

.no_inexact:

    mov     r9d,    eax
    shr     r9d,    5
    
    and     r9d,    HEX (00000300)
    or      r8d,    r9d

    mov     r9d,    eax
    and     r9d,    HEX (00008040)
    
    cmp     r9d,    HEX (00008040)
    jne     .not_dn_flush
    
    or      r8d,    HEX (01000000)
    jmp     .dn_done

.not_dn_flush:

    cmp     r9d,    HEX (00000040)
    jne     .not_dn_operands
    
    or      r8d,    HEX (02000000)
    jmp     .dn_done

.not_dn_operands:

    cmp     r9d,    HEX (00008000)
    jne     .dn_done
    
    or      r8d,    HEX (03000000)

.dn_done:

    and     edx,    HEX (0308031F)
    
    mov     r9d,    edx
    not     r9d
    
    and     r8d,    r9d
    and     ecx,    edx
    
    or      r8d,    ecx
    and     eax,    HEX (FFFF003F)

    test    r8d,    HEX (00000010)
    jz      .set_no_invalid
    
    or      eax,    HEX (00000080)

.set_no_invalid:

    test    r8d,    HEX (00080000)
    jz      .set_no_denormal
    
    or      eax,    HEX (00000100)

.set_no_denormal:

    test    r8d,    HEX (00000008)
    jz      .set_no_zerodivide
    
    or      eax,    HEX (00000200)

.set_no_zerodivide:

    test    r8d,    HEX (00000004)
    jz      .set_no_overflow
    
    or      eax,    HEX (00000400)

.set_no_overflow:

    test    r8d,    HEX (00000002)
    jz      .set_no_underflow
    
    or      eax,    HEX (00000800)

.set_no_underflow:

    test    r8d,    HEX (00000001)
    jz      .set_no_inexact
    
    or      eax,    HEX (00001000)

.set_no_inexact:

    mov     r9d,    r8d
    and     r9d,    HEX (00000300)
    
    shl     r9d,    5
    or      eax,    r9d
    
    mov     r9d,    r8d
    and     r9d,    HEX (03000000)
    
    cmp     r9d,    HEX (01000000)
    jne     .not_set_dn_flush
    
    or      eax,    HEX (00008040)
    jmp     .set_dn_done

.not_set_dn_flush:

    cmp     r9d,    HEX (02000000)
    jne     .not_set_dn_operands
    
    or      eax,    HEX (00000040)
    jmp     .set_dn_done

.not_set_dn_operands:

    cmp     r9d,    HEX (03000000)
    jne     .set_dn_done
    
    or      eax,    HEX (00008000)

.set_dn_done:

    mov     dword ptr [rsp],    eax
    ldmxcsr dword ptr [rsp]
    
    mov     eax,    r8d
    add     rsp,    16
    ret

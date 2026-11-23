;******************************************************************************
; @file             strpbrk.asm
;******************************************************************************
%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function        strpbrk
;******************************************************************************
global      strpbrk
strpbrk:

    mov     r8,     rcx

.L5:

    mov     al,     byte ptr [r8]
    
    test    al,     al
    jz      .L1
    
    mov     r9,     rdx

.L4:

    mov     r10b,   byte ptr [r9]
    
    test    r10b,   r10b
    jz      .L3
    
    cmp     al,     r10b
    je      .L2
    
    inc     r9
    jmp     .L4

.L3:

    inc     r8
    jmp     .L5

.L2:

    mov     rax,    r8
    ret

.L1:

    xor     rax,    rax
    ret

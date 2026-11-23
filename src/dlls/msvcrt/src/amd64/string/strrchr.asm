;******************************************************************************
; @file             strrchr.asm
;******************************************************************************
%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function       strrchr
;******************************************************************************
global      strrchr
strrchr:

    push    rbx
    push    rsi
    push    rdi
    
    mov     rax,    rdx
    mov     rdi,    rcx
    xor     rcx,    rcx
    
    and     rdi,    rdi
    jz      .L2

.L4:

    cmp     byte ptr [rdi],     al
    jne     .L3
    
    mov     rcx,    rdi
    
    inc     rdi
    jmp     .L4

.L3:
    
    cmp     byte ptr [rdi],     0
    je      .L2
    
    inc     rdi
    jmp     .L4

.L2:

    mov     rax,    rcx
    jmp     .L1

.L1:

    pop     rdi
    pop     rsi
    pop     rbx
    ret


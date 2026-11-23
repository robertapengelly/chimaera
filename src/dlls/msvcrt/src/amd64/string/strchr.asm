;******************************************************************************
; @file             strchr.asm
;******************************************************************************
%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function       strchr
;******************************************************************************
global      strchr
strchr:

    push    rbx
    push    rsi
    push    rdi
    
    mov     rax,    rdx
    mov     rdi,    rcx
    
    and     rdi,    rdi
    jz      .L2

.L4:

    cmp     byte ptr [rdi],     al
    je      .L3
    
    cmp     byte ptr [rdi],     0
    je      .L2
    
    inc     rdi
    jmp     .L4

.L3:

    mov     rax,    rdi
    jmp     .L1

.L2:

    xor     rax,    rax

.L1:

    pop     rdi
    pop     rsi
    pop     rbx
    ret


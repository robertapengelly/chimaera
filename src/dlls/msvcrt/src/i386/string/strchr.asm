;******************************************************************************
; @file             strchr.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function        _strchr
;******************************************************************************
global      _strchr
_strchr:

    push    ebp
    mov     ebp,    esp
    
    push    ebx
    push    esi
    push    edi
    
    mov     eax,    dword ptr [ebp + 12]
    mov     edi,    dword ptr [ebp + 8]
    
    and     edi,    edi
    jz      .L2

.L4:

    cmp     byte ptr [edi],     al
    je      .L3
    
    cmp     byte ptr [edi],     0
    je      .L2
    
    inc     edi
    jmp     .L4

.L3:

    mov     eax,    edi
    jmp     .L1

.L2:

    xor     eax,    eax

.L1:

    pop     edi
    pop     esi
    pop     ebx
    pop     ebp
    ret

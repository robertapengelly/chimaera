;******************************************************************************
; @file             strlen.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function        _strlen
;******************************************************************************
global      _strlen
_strlen:

    push    ebp
    mov     ebp,    esp
    
    push    esi
    push    ecx
    
    mov     esi,    dword ptr [ebp + 8]
    xor     ecx,    ecx

.L2:

    lodsb
    
    and     al,     al
    jz      .L1
    
    inc     ecx
    jmp     .L2

.L1:

    mov     eax,    ecx
    pop     ecx
    pop     esi
    pop     ebp
    ret

;******************************************************************************
; @file             _controlfp.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function        __controlfp
;******************************************************************************
global      __controlfp
__controlfp:

    push    ebp
    mov     ebp,    esp
    
    mov     eax,    dword ptr [ebp + 12]
    and     eax,    HEX (FFF7FFFF)
    
    push    eax
    push    dword ptr [ebp + 8]
    call    __control87
    
    add     esp,    8
    
    pop     ebp
    ret

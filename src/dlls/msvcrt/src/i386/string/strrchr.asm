;******************************************************************************
; @file             strrchr.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function        _strrchr
;******************************************************************************
global      _strrchr
_strrchr:

    push    ebp
    mov     ebp,    esp
    
    push    ebx
    push    esi
    push    edi
    
    mov     eax,    dword ptr [ebp + 12]
    mov     edi,    dword ptr [ebp + 8]
    xor     ecx,    ecx
    
    and     edi,    edi
    jz      .L2

.L4:

    cmp     byte ptr [edi],     al
    jne     .L3
    
    mov     ecx,    edi
    
    inc     edi
    jmp     .L4

.L3:

    cmp     byte ptr [edi],     0
    je      .L2
    
    inc     edi
    jmp     .L4

.L2:

    mov     eax,    ecx

.L1:

    pop     edi
    pop     esi
    pop     ebx
    pop     ebp
    ret

;******************************************************************************
; @file             strpbrk.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function        _strpbrk
;******************************************************************************
global      _strpbrk
_strpbrk:

    push    ebp
    mov     ebp,    esp
    
    push    esi
    push    edi
    
    mov     esi,    dword ptr [ebp + 8]                     ; str

.L6:

    mov     al,     byte ptr [esi]
    
    test    al,     al
    jz      .L2
    
    mov     edi,    dword ptr [ebp + 12]                    ; accept

.L5:

    mov     dl,     byte ptr [edi]
    
    test    dl,     dl
    jz      .L4
    
    cmp     al,     dl
    je      .L3
    
    inc     edi
    jmp     .L5

.L4:

    inc     esi
    jmp     .L6

.L3:

    mov     eax,    esi
    jmp     .L1

.L2:

    xor     eax,    eax

.L1:

    pop     edi
    pop     esi
    
    mov     esp,    ebp
    pop     ebp
    
    ret

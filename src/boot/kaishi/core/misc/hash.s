;******************************************************************************
; @file             hashtab.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function        _hashtab_generate_hash
;******************************************************************************
global      _hashtab_generate_hash
_hashtab_generate_hash:

    push    ebp
    
    mov     ebp,    esp
    sub     esp,    16
    
    push    ebx
    push    ecx
    push    esi
    
    mov     eax,    dword ptr [ebp + 8]
    mov     dword ptr [ebp - 4],    eax
    
    mov     dword ptr [ebp - 8],    HEX (000AF93A)
    mov     dword ptr [ebp - 12],   0
    
    jmp     .L1

.L2:

    mov     edx,    dword ptr [ebp - 4]
    mov     eax,    dword ptr [ebp - 12]
    
    add     eax,    edx
    
    movzx   eax,    byte ptr [eax]
    movzx   eax,    al
    
    sal     eax,    4
    mov     edx,    eax
    
    mov     eax,    dword ptr [ebp - 8]
    shr     eax,    9
    add     edx,    eax
    
    mov     eax,    dword ptr [ebp - 8]
    add     edx,    eax
    
    mov     eax,    dword ptr [ebp - 8]
    shr     eax,    3
    
    lea     ecx,    [edx + eax]
    
    mov     edx,    dword ptr [ebp - 4]
    mov     eax,    dword ptr [ebp - 12]
    
    add     eax,    edx
    
    movzx   eax,    byte ptr [eax]
    movzx   eax,    al
    
    sal     eax,    2
    lea     edx,    [ecx + eax]
    
    mov     eax,    dword ptr [ebp - 8]
    sal     eax,    24
    mov     ecx,    eax
    
    mov     eax,    edx
    sub     eax,    ecx
    mov     dword ptr [ebp - 8],    eax
    
    inc     dword ptr [ebp - 12]

.L1:

    mov     eax,    dword ptr [ebp - 12]
    
    cmp     eax,    dword ptr [ebp + 12]
    jb      .L2
    
    mov     eax,    dword ptr [ebp - 8]
    
    pop     esi
    pop     ecx
    pop     ebx
    
    add     esp,     16
    pop     ebp
    
    ret

;******************************************************************************
; @file             time.asm
;******************************************************************************
.option     segment:use32
.386

;******************************************************************************
; @function        _time
;******************************************************************************
global      _time
_time:

    push    ebx
    sub     esp,    8
    
    lea     eax,    [esp]
    
    push    eax
    call    _NtQuerySystemTime@4
    
    test    eax,    eax
    jnz     .status_failed
    
    mov     eax,    dword ptr [esp]
    mov     edx,    dword ptr [esp + 4]
    
    sub     eax,    0xD53E8000
    sbb     edx,    0x019DB1DE
    jc      .failed
    
    mov     ebx,    10000000
    
    cmp     edx,    ebx
    jae     .failed
    
    div     ebx
    
    cmp     eax,    0x7FFFFFFF
    ja      .failed
    
    mov     edx,    dword ptr [esp + 16]
    
    test    edx,    edx
    jz      .done
    
    mov     dword ptr [edx],    eax
    jmp     .done

.failed:

    mov     eax,    0xC0000095

.status_failed:

    push    eax
    call    ___msvcrt_set_errno
    
    add     esp,    4
    mov     eax,    -1

.done:

    add     esp,    8
    pop     ebx
    ret

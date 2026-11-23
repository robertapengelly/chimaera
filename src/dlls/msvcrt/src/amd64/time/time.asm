;******************************************************************************
; @file             time.asm
;******************************************************************************

;******************************************************************************
; @function        time
;******************************************************************************
global      time
time:

    push    rbx
    
    mov     rbx,    rcx
    sub     rsp,    48
    
    lea     rcx,    [rsp + 32]
    call    NtQuerySystemTime
    
    test    eax,    eax
    jnz     .status_failed
    
    mov     rax,    qword ptr [rsp + 32]
    mov     rcx,    116444736000000000
    
    cmp     rax,    rcx
    jb      .failed
    
    sub     rax,    rcx
    
    mov     rcx,    10000000
    xor     rdx,    rdx
    
    div     rcx
    
    test    rbx,    rbx
    jz      .done
    
    mov     qword ptr [rbx],    rax
    jmp     .done

.failed:

    mov     eax,    0xC0000095

.status_failed:

    mov     ecx,    eax
    call    __msvcrt_set_errno
    
    mov     rax,    -1

.done:

    add     rsp,    48
    pop     rbx
    ret

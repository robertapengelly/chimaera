;******************************************************************************
; @file             NtQuerySystemTime.asm
;******************************************************************************
%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

%define     STATUS_UNSUCCESSFUL                             HEX (C0000001)
%define     STATUS_INVALID_PARAMETER                        HEX (C000000D)
%define     STATUS_IO_DEVICE_ERROR                          HEX (C0000185)
%define     STATUS_ACCESS_VIOLATION                         HEX (C0000005)
%define     STATUS_NOT_IMPLEMENTED                          HEX (C0000002)

;******************************************************************************
; @function        NtQuerySystemTime
;******************************************************************************
global      NtQuerySystemTime
NtQuerySystemTime:

    test    rcx,    rcx
    jz      .invalid
    
    push    rdi
    push    rbx
    mov     rbx,    rcx
    
    xor     edi,    edi
    mov     eax,    HEX (C9)
    syscall
    
    test    rax,    rax
    js      .failed
    
    mov     rcx,    10000000
    imul    rax,    rcx
    
    mov     rcx,    116444736000000000
    add     rax,    rcx
    
    mov     qword ptr [rbx],    rax
    
    xor     eax,    eax
    jmp     .done

.failed:

    mov     edx,    STATUS_IO_DEVICE_ERROR
    
    cmp     rax,    -5
    je      .time_status
    
    mov     edx,    STATUS_ACCESS_VIOLATION
    
    cmp     rax,    -14
    je      .time_status
    
    mov     edx,    STATUS_INVALID_PARAMETER
    
    cmp     rax,    -22
    je      .time_status
    
    mov     edx,    STATUS_NOT_IMPLEMENTED
    
    cmp     rax,    -38
    je      .time_status
    
    mov     edx,    STATUS_UNSUCCESSFUL

.time_status:

    mov     eax,    edx

.done:

    pop     rbx
    pop     rdi
    ret

.invalid:

    mov     eax,    STATUS_INVALID_PARAMETER
    ret

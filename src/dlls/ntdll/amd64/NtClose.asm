;******************************************************************************
; @file             NtClose.asm
;******************************************************************************
%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

%define     STATUS_SUCCESS                                  HEX (00000000)
%define     STATUS_UNSUCCESSFUL                             HEX (C0000001)
%define     STATUS_INVALID_PARAMETER                        HEX (C000000D)
%define     STATUS_INVALID_HANDLE                           HEX (C0000008)
%define     STATUS_IO_DEVICE_ERROR                          HEX (C0000185)
%define     STATUS_NOT_IMPLEMENTED                          HEX (C0000002)

;******************************************************************************
; @function        NtClose
;******************************************************************************
global      NtClose
NtClose:

    push    rdi
    
    mov     rdi,    rcx
    mov     eax,    HEX (03)
    syscall
    
    pop     rdi
    
    test    rax,    rax
    jnz     .failed
    
    xor     eax,    eax
    ret

.failed:

    mov     edx,    STATUS_INVALID_HANDLE
    
    cmp     rax,    -9
    je      .close_status
    
    mov     edx,    STATUS_IO_DEVICE_ERROR
    
    cmp     rax,    -5
    je      .close_status
    
    mov     edx,    STATUS_NOT_IMPLEMENTED
    
    cmp     rax,    -38
    je      .close_status
    
    mov     edx,    STATUS_UNSUCCESSFUL

.close_status:

    mov     eax,    edx
    ret

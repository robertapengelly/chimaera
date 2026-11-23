;******************************************************************************
; @file             NtWriteFile.asm
;******************************************************************************
%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

%define     STATUS_SUCCESS                                  HEX (00000000)
%define     STATUS_UNSUCCESSFUL                             HEX (C0000001)
%define     STATUS_INVALID_PARAMETER                        HEX (C000000D)
%define     STATUS_INVALID_HANDLE                           HEX (C0000008)
%define     STATUS_IO_DEVICE_ERROR                          HEX (C0000185)
%define     STATUS_DEVICE_NOT_READY                         HEX (C00000A3)
%define     STATUS_NO_MEMORY                                HEX (C0000017)
%define     STATUS_ACCESS_DENIED                            HEX (C0000022)
%define     STATUS_ACCESS_VIOLATION                         HEX (C0000005)
%define     STATUS_DEVICE_BUSY                              HEX (80000011)
%define     STATUS_NO_SUCH_DEVICE                           HEX (C000000E)
%define     STATUS_DISK_FULL                                HEX (C000007F)
%define     STATUS_MEDIA_WRITE_PROTECTED                    HEX (C00000A2)
%define     STATUS_NOT_IMPLEMENTED                          HEX (C0000002)
%define     STATUS_INTEGER_OVERFLOW                         HEX (C0000095)

;******************************************************************************
; @function        NtWriteFile
;******************************************************************************
global      NtWriteFile
NtWriteFile:

    push    rbp
    mov     rbp,    rsp
    
    push    rdi
    push    rsi
    push    r12
    
    mov     r12,    qword ptr [rbp + 48]
    
    test    r12,    r12
    jz      .invalid
    
    mov     rsi,    qword ptr [rbp + 56]
    
    test    rsi,    rsi
    jz      .invalid
    
    mov     edx,    dword ptr [rbp + 64]
    mov     rdi,    rcx
    
    mov     eax,    HEX (01)
    syscall
    
    test    rax,    rax
    js      .failed
    
    mov     dword ptr [r12 + 0],    STATUS_SUCCESS
    mov     qword ptr [r12 + 8],    rax
    
    xor     eax,    eax
    jmp     .done

.failed:

    mov     edx,    STATUS_IO_DEVICE_ERROR
    
    cmp     rax,    -5
    je      .io_error
    
    mov     edx,    STATUS_INVALID_HANDLE
    
    cmp     rax,    -9
    je      .io_error
    
    mov     edx,    STATUS_DEVICE_NOT_READY
    
    cmp     rax,    -11
    je      .io_error
    
    mov     edx,    STATUS_NO_MEMORY
    
    cmp     rax,    -12
    je      .io_error
    
    mov     edx,    STATUS_ACCESS_DENIED
    
    cmp     rax,    -13
    je      .io_error
    
    mov     edx,    STATUS_ACCESS_VIOLATION
    
    cmp     rax,    -14
    je      .io_error
    
    mov     edx,    STATUS_DEVICE_BUSY
    
    cmp     rax,    -16
    je      .io_error
    
    mov     edx,    STATUS_NO_SUCH_DEVICE
    
    cmp     rax,    -19
    je      .io_error
    
    mov     edx,    STATUS_INVALID_PARAMETER
    
    cmp     rax,    -22
    je      .io_error
    
    mov     edx,    STATUS_DISK_FULL
    
    cmp     rax,    -28
    je      .io_error
    
    mov     edx,    STATUS_MEDIA_WRITE_PROTECTED
    
    cmp     rax,    -30
    je      .io_error
    
    mov     edx,    STATUS_NOT_IMPLEMENTED
    
    cmp     rax,    -38
    je      .io_error
    
    mov     edx,    STATUS_INTEGER_OVERFLOW
    
    cmp     rax,    -75
    je      .io_error
    
    mov     edx,    STATUS_UNSUCCESSFUL

.io_error:

    mov     eax,    edx
    
    mov     dword ptr [r12 + 0],    eax
    mov     qword ptr [r12 + 8],    0
    
    jmp     .done

.invalid:

    mov     eax,    STATUS_INVALID_PARAMETER

.done:

    pop     r12
    pop     rsi
    pop     rdi
    pop     rbp
    ret

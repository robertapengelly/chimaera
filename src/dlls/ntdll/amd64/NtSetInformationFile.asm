;******************************************************************************
; @file             NtSetInformationFile.asm
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
%define     STATUS_INTEGER_OVERFLOW                         HEX (C0000095)

%define     FilePositionInformation                         14

;******************************************************************************
; @function        NtSetInformationFile
;******************************************************************************
global      NtSetInformationFile
NtSetInformationFile:

    push    rbp
    mov     rbp,    rsp
    
    push    rdi
    push    rsi
    push    r12
    
    mov     rdi,    rcx
    mov     r12,    rdx
    
    test    r12,    r12
    jz      .invalid
    
    test    r8,    r8
    jz      .invalid
    
    cmp     r9d,    8
    jb      .invalid
    
    cmp     dword ptr [rbp + 48],    FilePositionInformation
    jne     .invalid
    
    mov     rsi,    qword ptr [r8]
    
    test    rsi,    rsi
    js      .invalid
    
    mov     eax,    HEX (08)
    xor     edx,    edx
    syscall
    
    test    rax,    rax
    js      .failed
    
    mov     dword ptr [r12 + 0],    STATUS_SUCCESS
    mov     qword ptr [r12 + 8],    0
    
    xor     eax,    eax
    jmp     .done

.failed:

    mov     edx,    STATUS_INVALID_HANDLE
    
    cmp     rax,    -9
    je      .status
    
    mov     edx,    STATUS_INVALID_PARAMETER
    
    cmp     rax,    -22
    je      .status
    
    cmp     rax,    -29
    je      .status
    
    mov     edx,    STATUS_IO_DEVICE_ERROR
    
    cmp     rax,    -5
    je      .status
    
    mov     edx,    STATUS_NOT_IMPLEMENTED
    
    cmp     rax,    -38
    je      .status
    
    mov     edx,    STATUS_INTEGER_OVERFLOW
    
    cmp     rax,    -75
    je      .status
    
    mov     edx,    STATUS_UNSUCCESSFUL

.status:

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

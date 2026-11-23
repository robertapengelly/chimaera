;******************************************************************************
; @file             NtQueryInformationFile.asm
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

%define     FileStandardInformation                         5
%define     FilePositionInformation                         14

;******************************************************************************
; @function        NtQueryInformationFile
;******************************************************************************
global      NtQueryInformationFile
NtQueryInformationFile:

    push    rbp
    mov     rbp,    rsp
    
    push    rdi
    push    rsi
    push    r12
    push    r13
    push    r14
    push    r15
    
    mov     rdi,    rcx
    mov     r12,    rdx
    mov     r13,    r8
    
    test    r12,    r12
    jz      .invalid
    
    test    r13,    r13
    jz      .invalid
    
    cmp     dword ptr [rbp + 48],    FilePositionInformation
    je      .position
    
    cmp     dword ptr [rbp + 48],    FileStandardInformation
    jne     .invalid
    
    cmp     r9d,    24
    jb      .invalid
    
    mov     eax,    HEX (08)
    xor     esi,    esi
    
    mov     edx,    1
    syscall
    
    test    rax,    rax
    js      .failed
    
    mov     r14,    rax
    
    mov     eax,    HEX (08)
    xor     esi,    esi
    
    mov     edx,    2
    syscall
    
    test    rax,    rax
    js      .failed
    
    mov     r15,    rax
    mov     rsi,    r14
    
    mov     eax,    HEX (08)
    xor     edx,    edx
    syscall
    
    test    rax,    rax
    js      .failed
    
    mov     qword ptr [r13 + 0],    r15
    mov     qword ptr [r13 + 8],    r15
    mov     dword ptr [r13 + 16],   1
    mov     dword ptr [r13 + 20],   0
    
    mov     dword ptr [r12 + 0],    STATUS_SUCCESS
    mov     qword ptr [r12 + 8],    24
    
    xor     eax,    eax
    jmp     .done

.position:

    cmp     r9d,    8
    jb      .invalid
    
    mov     eax,    HEX (08)
    xor     esi,    esi
    
    mov     edx,    1
    syscall
    
    test    rax,    rax
    js      .failed
    
    mov     qword ptr [r13 + 0],    rax
    mov     dword ptr [r12 + 0],    STATUS_SUCCESS
    mov     qword ptr [r12 + 8],    8
    
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

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rsi
    pop     rdi
    pop     rbp
    ret

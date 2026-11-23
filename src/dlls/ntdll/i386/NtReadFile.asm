;******************************************************************************
; @file             NtReadFile.asm
;******************************************************************************
.option     segment:use32
.386

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
; @function        _NtReadFile@36
;******************************************************************************
global      _NtReadFile@36
_NtReadFile@36:

    push    ebp
    mov     ebp,    esp
    
    push    ebx
    push    esi
    
    mov     esi,    dword ptr [ebp + 24]
    
    test    esi,    esi
    jz      .invalid
    
    mov     ecx,    dword ptr [ebp + 28]
    
    test    ecx,    ecx
    jz      .invalid
    
    mov     edx,    dword ptr [ebp + 32]
    mov     ebx,    dword ptr [ebp + 8]
    
    mov     eax,    HEX (03)
    int     HEX (80)
    
    test    eax,    eax
    js      .failed
    
    mov     dword ptr [esi + 0],    STATUS_SUCCESS
    mov     dword ptr [esi + 4],    eax
    
    xor     eax,    eax
    jmp     .done

.failed:

    mov     edx,    STATUS_IO_DEVICE_ERROR
    
    cmp     eax,    -5
    je      .io_error
    
    mov     edx,    STATUS_INVALID_HANDLE
    
    cmp     eax,    -9
    je      .io_error
    
    mov     edx,    STATUS_DEVICE_NOT_READY
    
    cmp     eax,    -11
    je      .io_error
    
    mov     edx,    STATUS_NO_MEMORY
    
    cmp     eax,    -12
    je      .io_error
    
    mov     edx,    STATUS_ACCESS_DENIED
    
    cmp     eax,    -13
    je      .io_error
    
    mov     edx,    STATUS_ACCESS_VIOLATION
    
    cmp     eax,    -14
    je      .io_error
    
    mov     edx,    STATUS_DEVICE_BUSY
    
    cmp     eax,    -16
    je      .io_error
    
    mov     edx,    STATUS_NO_SUCH_DEVICE
    
    cmp     eax,    -19
    je      .io_error
    
    mov     edx,    STATUS_INVALID_PARAMETER
    
    cmp     eax,    -22
    je      .io_error
    
    mov     edx,    STATUS_DISK_FULL
    
    cmp     eax,    -28
    je      .io_error
    
    mov     edx,    STATUS_MEDIA_WRITE_PROTECTED
    
    cmp     eax,    -30
    je      .io_error
    
    mov     edx,    STATUS_NOT_IMPLEMENTED
    
    cmp     eax,    -38
    je      .io_error
    
    mov     edx,    STATUS_INTEGER_OVERFLOW
    
    cmp     eax,    -75
    je      .io_error
    
    mov     edx,    STATUS_UNSUCCESSFUL

.io_error:

    mov     eax,    edx
    
    mov     dword ptr [esi + 0],    eax
    mov     dword ptr [esi + 4],    0
    
    jmp     .done

.invalid:

    mov     eax,    STATUS_INVALID_PARAMETER

.done:

    pop     esi
    pop     ebx
    pop     ebp
    ret     36

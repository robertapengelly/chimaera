;******************************************************************************
; @file             NtClose.asm
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
%define     STATUS_NOT_IMPLEMENTED                          HEX (C0000002)

;******************************************************************************
; @function        _NtClose@4
;******************************************************************************
global      _NtClose@4
_NtClose@4:

    push    ebx
    mov     ebx,    dword ptr [esp + 8]
    
    mov     eax,    HEX (06)
    int     HEX (80)
    
    pop     ebx
    
    test    eax,    eax
    jnz     .failed
    
    xor     eax,    eax
    ret     4

.failed:

    mov     edx,    STATUS_INVALID_HANDLE
    
    cmp     eax,    -9
    je      .close_status
    
    mov     edx,    STATUS_IO_DEVICE_ERROR
    
    cmp     eax,    -5
    je      .close_status
    
    mov     edx,    STATUS_NOT_IMPLEMENTED
    
    cmp     eax,    -38
    je      .close_status
    
    mov     edx,    STATUS_UNSUCCESSFUL

.close_status:

    mov     eax,    edx
    ret     4

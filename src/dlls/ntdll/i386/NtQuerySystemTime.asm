;******************************************************************************
; @file             NtQuerySystemTime.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

%define     STATUS_UNSUCCESSFUL                             HEX (C0000001)
%define     STATUS_INVALID_PARAMETER                        HEX (C000000D)
%define     STATUS_IO_DEVICE_ERROR                          HEX (C0000185)
%define     STATUS_ACCESS_VIOLATION                         HEX (C0000005)
%define     STATUS_NOT_IMPLEMENTED                          HEX (C0000002)

;******************************************************************************
; @function        _NtQuerySystemTime@4
;******************************************************************************
global      _NtQuerySystemTime@4
_NtQuerySystemTime@4:

    push    ebx
    mov     ecx,    dword ptr [esp + 8]
    
    test    ecx,    ecx
    jz      .invalid
    
    xor     ebx,    ebx
    
    mov     eax,    HEX (0D)
    int     HEX (80)
    
    test    eax,    eax
    js      .failed
    
    mov     ecx,    10000000
    mul     ecx
    
    add     eax,    HEX (D53E8000)
    adc     edx,    HEX (019DB1DE)
    
    mov     ecx,    dword ptr [esp + 8]
    
    mov     dword ptr [ecx],    eax
    mov     dword ptr [ecx + 4],    edx
    
    xor     eax,    eax
    jmp     .done

.failed:

    mov     edx,    STATUS_IO_DEVICE_ERROR
    
    cmp     eax,    -5
    je      .time_status
    
    mov     edx,    STATUS_ACCESS_VIOLATION
    
    cmp     eax,    -14
    je      .time_status
    
    mov     edx,    STATUS_INVALID_PARAMETER
    
    cmp     eax,    -22
    je      .time_status
    
    mov     edx,    STATUS_NOT_IMPLEMENTED
    
    cmp     eax,    -38
    je      .time_status
    
    mov     edx,    STATUS_UNSUCCESSFUL

.time_status:

    mov     eax,    edx
    jmp     .done

.invalid:

    mov     eax,    STATUS_INVALID_PARAMETER

.done:

    pop     ebx
    ret     4

;******************************************************************************
; @file             NtSetInformationFile.asm
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
%define     STATUS_INTEGER_OVERFLOW                         HEX (C0000095)

%define     FilePositionInformation                         14

;******************************************************************************
; @function        _NtSetInformationFile@20
;******************************************************************************
global      _NtSetInformationFile@20
_NtSetInformationFile@20:

    push    ebp
    mov     ebp,    esp
    
    push    ebx
    push    esi
    
    mov     esi,    dword ptr [ebp + 12]
    
    test    esi,    esi
    jz      .invalid
    
    mov     edx,    dword ptr [ebp + 16]
    
    test    edx,    edx
    jz      .invalid
    
    cmp     dword ptr [ebp + 20],    8
    jb      .invalid
    
    cmp     dword ptr [ebp + 24],    FilePositionInformation
    jne     .invalid
    
    cmp     dword ptr [edx + 4],    0
    jne     .overflow
    
    mov     ecx,    dword ptr [edx]
    
    test    ecx,    ecx
    js      .overflow
    
    mov     ebx,    dword ptr [ebp + 8]
    mov     eax,    HEX (13)
    
    xor     edx,    edx
    int     HEX (80)
    
    test    eax,    eax
    js      .failed
    
    mov     dword ptr [esi + 0],    STATUS_SUCCESS
    mov     dword ptr [esi + 4],    0
    
    xor     eax,    eax
    jmp     .done

.overflow:

    mov     eax,    STATUS_INTEGER_OVERFLOW
    jmp     .done

.failed:

    mov     edx,    STATUS_INVALID_HANDLE
    
    cmp     eax,    -9
    je      .status
    
    mov     edx,    STATUS_INVALID_PARAMETER
    
    cmp     eax,    -22
    je      .status
    
    cmp     eax,    -29
    je      .status
    
    mov     edx,    STATUS_IO_DEVICE_ERROR
    
    cmp     eax,    -5
    je      .status
    
    mov     edx,    STATUS_NOT_IMPLEMENTED
    
    cmp     eax,    -38
    je      .status
    
    mov     edx,    STATUS_INTEGER_OVERFLOW
    
    cmp     eax,    -75
    je      .status
    
    mov     edx,    STATUS_UNSUCCESSFUL

.status:

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
    ret     20

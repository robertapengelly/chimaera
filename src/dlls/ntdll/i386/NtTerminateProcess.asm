;******************************************************************************
; @file             NtTerminateProcess.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

%define     STATUS_INVALID_HANDLE                           HEX (C0000008)
%define     STATUS_NOT_IMPLEMENTED                          HEX (C0000002)

;******************************************************************************
; @function        _NtTerminateProcess@8
;******************************************************************************
global      _NtTerminateProcess@8
_NtTerminateProcess@8:

    push    ebx
    mov     eax,    dword ptr [esp + 8]
    
    test    eax,    eax
    jz      .terminate
    
    cmp     eax,    -1
    jne     .invalid

.terminate:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Invoke the current-process exit syscall.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ebx,    dword ptr [esp + 12]
    mov     eax,    HEX (01)
    int     HEX (80)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; The syscall returns only if termination is unsupported.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    STATUS_NOT_IMPLEMENTED
    jmp     .done

.invalid:

    mov     eax,    STATUS_INVALID_HANDLE

.done:

    pop     ebx
    ret     8

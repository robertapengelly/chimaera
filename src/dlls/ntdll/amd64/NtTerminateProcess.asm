;******************************************************************************
; @file             NtTerminateProcess.asm
;******************************************************************************
%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

%define     STATUS_INVALID_HANDLE       HEX (C0000008)
%define     STATUS_NOT_IMPLEMENTED      HEX (C0000002)

;******************************************************************************
; @function        NtTerminateProcess
;******************************************************************************
global      NtTerminateProcess
NtTerminateProcess:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Only the current-process pseudo-handle is supported.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    test    rcx,    rcx
    jz      .terminate
    
    cmp     rcx,    -1
    jne     .invalid

.terminate:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Invoke the current-process exit syscall.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    rdi
    
    mov     edi,    edx
    mov     eax,    HEX (3C)
    
    syscall
    pop     rdi
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; The syscall returns only if termination is unsupported.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    STATUS_NOT_IMPLEMENTED
    ret

.invalid:

    mov     eax,    STATUS_INVALID_HANDLE
    ret

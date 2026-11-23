;******************************************************************************
; @file             crt0.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;******************************************************************************
; @function         _mainCRTStartup
;******************************************************************************
global      _mainCRTStartup
_mainCRTStartup:

    push    ebp
    
    mov     ebp,    esp
    sub     esp,    16
    
    mov     dword ptr [ebp - 16],    0
    mov     dword ptr [ebp - 8],     0
    mov     dword ptr [ebp - 4],     0
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Retrieve the command-line arguments and environment.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    lea     eax,    [ebp - 16]
    
    push    eax
    push    0
    
    lea     eax,    [ebp - 12]
    push    eax
    
    lea     eax,    [ebp - 8]
    push    eax
    
    lea     eax,    [ebp - 4]
    push    eax
    
    call    ___getmainargs
    add     esp,    20
    
    cmp     dword ptr [ebp - 4],     1
    jl      .failed
    
    cmp     dword ptr [ebp - 8],     0
    je      .failed
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Call the application's main function.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    dword ptr [ebp - 12]
    push    dword ptr [ebp - 8]
    push    dword ptr [ebp - 4]
    
    call    _main
    add     esp,    12
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Terminate the process with main's return value.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    eax
    call    _exit
    
    jmp     _wait

.failed:

    push    1
    call    _exit

;******************************************************************************
; @function         _wait
;******************************************************************************
_wait:

    jmp     _wait

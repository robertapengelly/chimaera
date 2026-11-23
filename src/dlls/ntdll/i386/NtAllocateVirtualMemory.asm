;******************************************************************************
; @file             NtAllocateVirtualMemory.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Helper defines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define     PROT_NONE                   0
%define     PROT_READ                   1
%define     PROT_WRITE                  2
%define     PROT_EXEC                   4

%define     MAP_ANONYMOUS               HEX (20)
%define     MAP_PRIVATE                 HEX (02)
%define     MAP_32BIT                   HEX (00)

%define     PAGE_NOACCESS               HEX (01)
%define     PAGE_READONLY               HEX (02)
%define     PAGE_READWRITE              HEX (04)
%define     PAGE_WRITECOPY              HEX (08)
%define     PAGE_EXECUTE                HEX (10)
%define     PAGE_EXECUTE_READ           HEX (20)
%define     PAGE_EXECUTE_READWRITE      HEX (40)
%define     PAGE_EXECUTE_WRITECOPY      HEX (80)

%define     STATUS_SUCCESS                                  HEX (00000000)
%define     STATUS_INVALID_PARAMETER                        HEX (C000000D)
%define     STATUS_NO_MEMORY                                HEX (C0000017)
%define     STATUS_ACCESS_VIOLATION                         HEX (C0000005)
%define     STATUS_NO_SUCH_DEVICE                           HEX (C000000E)
%define     STATUS_NOT_IMPLEMENTED                          HEX (C0000002)

;******************************************************************************
; @function        _NtAllocateVirtualMemory@24
;******************************************************************************
global      _NtAllocateVirtualMemory@24
_NtAllocateVirtualMemory@24:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the base pointer.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ebp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the base pointer with the stack pointer.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ebp,    esp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Reserve memory for mmap arguments.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    sub     esp,    24
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ebx
    push    esi
    push    edi
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get BaseAddress and RegionSize.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     esi,    dword ptr [ebp + 12]
    mov     edi,    dword ptr [ebp + 20]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Validate pointers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    and     esi,    esi
    jz      .invalid
    
    and     edi,    edi
    jz      .invalid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get requested region size.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    dword ptr [edi]
    
    and     eax,    eax
    jz      .invalid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Set mmap address and length.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ebx,    dword ptr [esi]
    mov     dword ptr [ebp - 24],   ebx
    mov     dword ptr [ebp - 20],   eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Convert PAGE_* protection to PROT_*.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    dword ptr [ebp + 28]
    
    cmp     eax,    PAGE_NOACCESS
    je      .protect_none
    
    cmp     eax,    PAGE_READONLY
    je      .protect_read
    
    cmp     eax,    PAGE_READWRITE
    je      .protect_readwrite
    
    cmp     eax,    PAGE_WRITECOPY
    je      .protect_readwrite
    
    cmp     eax,    PAGE_EXECUTE
    je      .protect_execute
    
    cmp     eax,    PAGE_EXECUTE_READ
    je      .protect_executeread
    
    cmp     eax,    PAGE_EXECUTE_READWRITE
    je      .protect_executereadwrite
    
    cmp     eax,    PAGE_EXECUTE_WRITECOPY
    je      .protect_executereadwrite
    
    jmp     .invalid

.protect_none:

    mov     dword ptr [ebp - 16],   PROT_NONE
    jmp     .protect_done

.protect_read:

    mov     dword ptr [ebp - 16],   PROT_READ
    jmp     .protect_done

.protect_readwrite:

    mov     dword ptr [ebp - 16],   PROT_READ | PROT_WRITE
    jmp     .protect_done

.protect_execute:

    mov     dword ptr [ebp - 16],   PROT_EXEC
    jmp     .protect_done

.protect_executeread:

    mov     dword ptr [ebp - 16],   PROT_READ | PROT_EXEC
    jmp     .protect_done

.protect_executereadwrite:

    mov     dword ptr [ebp - 16],   PROT_READ | PROT_WRITE | PROT_EXEC

.protect_done:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Set mmap flags.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     dword ptr [ebp - 12],   MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT
    
    mov     dword ptr [ebp - 8],   -1
    mov     dword ptr [ebp - 4],   0
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Pass mmap argument structure.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    lea     ebx,    [ebp - 24]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Invoke int 0x80.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    HEX (5A)
    int     HEX (80)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Check for failure.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cmp     eax,    -4095
    jae     .allocation_failed
    
    and     eax,    eax
    jz      .no_memory
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return resulting address through BaseAddress.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     dword ptr [esi],    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; RegionSize remains the requested size.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    dword ptr [ebp - 20]
    mov     dword ptr [edi],    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return STATUS_SUCCESS.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     eax,    eax
    jmp     .done

.invalid:

    mov     eax,    STATUS_INVALID_PARAMETER
    jmp     .done

.allocation_failed:

    cmp     eax,    -22
    je      .invalid
    
    cmp     eax,    -14
    je      .access_violation
    
    cmp     eax,    -19
    je      .no_device
    
    cmp     eax,    -38
    je      .not_implemented
    
    jmp     .no_memory

.access_violation:

    mov     eax,    STATUS_ACCESS_VIOLATION
    jmp     .done

.no_device:

    mov     eax,    STATUS_NO_SUCH_DEVICE
    jmp     .done

.not_implemented:

    mov     eax,    STATUS_NOT_IMPLEMENTED
    jmp     .done

.no_memory:

    mov     eax,    STATUS_NO_MEMORY

.done:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     edi
    pop     esi
    pop     ebx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Clean up the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     esp,    24
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the base pointer.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     ebp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return to caller.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ret     24

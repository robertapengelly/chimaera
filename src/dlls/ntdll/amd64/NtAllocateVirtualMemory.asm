;******************************************************************************
; @file             NtAllocateVirtualMemory.asm
;******************************************************************************
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
%define     MAP_32BIT                   HEX (40)

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
; @function        NtAllocateVirtualMemory
;******************************************************************************
global      NtAllocateVirtualMemory
NtAllocateVirtualMemory:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    rbx
    push    r12
    push    r13
    push    r14
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save BaseAddress and RegionSize.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     r12,    rdx
    mov     r13,    r9
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Validate pointers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    and     r12,    r12
    jz      .invalid
    
    and     r13,    r13
    jz      .invalid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get requested region size.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     r14,    qword ptr [r13]
    
    and     r14,    r14
    jz      .invalid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get Windows protection argument.
    ;;
    ;; Four registers were pushed, therefore the sixth original argument is
    ;; now at rsp + 72.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    dword ptr [rsp + 72]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Convert PAGE_* protection to PROT_*.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     r8,     r8
    
    cmp     eax,    PAGE_NOACCESS
    je      .protect_done
    
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

.protect_read:

    mov     r8,     PROT_READ
    jmp     .protect_done

.protect_readwrite:

    mov     r8,     PROT_READ | PROT_WRITE
    jmp     .protect_done

.protect_execute:

    mov     r8,     PROT_EXEC
    jmp     .protect_done

.protect_executeread:

    mov     r8,     PROT_READ | PROT_EXEC
    jmp     .protect_done

.protect_executereadwrite:

    mov     r8,     PROT_READ | PROT_WRITE | PROT_EXEC

.protect_done:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    rdi
    push    rsi
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Create the mmap call.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     rdi,    qword ptr [r12]
    mov     rsi,    r14
    mov     rdx,    r8
    mov     r10,    MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT
    mov     r8,     -1
    xor     r9,     r9
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Invoke mmap syscall.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     rax,    HEX (09)
    syscall
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     rsi
    pop     rdi
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Check for failure.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cmp     rax,    -4095
    jae     .allocation_failed
    
    and     rax,    rax
    jz      .no_memory
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return resulting address through BaseAddress.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     qword ptr [r12],    rax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return the region size.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     qword ptr [r13],    r14
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return STATUS_SUCCESS.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     rax,    rax
    jmp     .done

.invalid:

    mov     rax,    STATUS_INVALID_PARAMETER
    jmp     .done

.allocation_failed:

    cmp     rax,    -22
    je      .invalid
    
    cmp     rax,    -14
    je      .access_violation
    
    cmp     rax,    -19
    je      .no_device
    
    cmp     rax,    -38
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

    mov     rax,    STATUS_NO_MEMORY

.done:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return to caller.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ret

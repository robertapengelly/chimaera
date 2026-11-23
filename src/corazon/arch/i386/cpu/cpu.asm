;******************************************************************************
; @file             cpu.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; CPU feature definitions.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CR_EAX              equ     (1 << 5)
CR_EBX              equ     (1 << 6)
CR_ECX              equ     (1 << 7)
CR_EDX              equ     (1 << 8)

CF_CPUID            equ     0

CF_RDTSC            equ     (CR_EDX | 4)
CF_MSR              equ     (CR_EDX | 5)
CF_PSE              equ     (CR_EDX | 3)
CF_APIC             equ     (CR_EDX | 9)
CF_PGE              equ     (CR_EDX | 13)
CF_FXSR             equ     (CR_EDX | 24)
CF_SSE              equ     (CR_EDX | 25)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Static data.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
.data

align   4
cpuid_available:

    dd      0

supports_rdtsc:

    dd      0

.code

;******************************************************************************
; @function         eflags_bit_changeable
;
; @param            bit
;                   EFLAGS bit mask to test.
;
; @return           eax
;                   0 if the bit cannot be changed, 1 otherwise.
;******************************************************************************
eflags_bit_changeable:

    push    ebp
    mov     ebp,    esp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get the original EFLAGS.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pushfd
    pop     eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Toggle the requested bit.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     edx,    eax
    xor     edx,    dword ptr [ebp + 8]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Attempt to update EFLAGS.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    edx
    popfd
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Read EFLAGS back.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pushfd
    pop     ecx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore the original EFLAGS.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    eax
    popfd
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Check whether the requested bit changed.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    xor     eax,    ecx
    and     eax,    dword ptr [ebp + 8]
    
    jz      .Leflags_no
    
    mov     eax,    1
    jmp     short .Leflags_done

.Leflags_no:

    xor     eax,    eax

.Leflags_done:

    mov     esp,    ebp
    pop     ebp
    ret

;******************************************************************************
; @function         _cpu_install
;******************************************************************************
global      _cpu_install
_cpu_install:

    push    ebp
    mov     ebp,    esp
    push    ebx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; EFLAGS.AC cannot be changed on an 80386. If it can be changed, we're
    ;; running on a 486 or later.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    HEX (00040000)
    call    eflags_bit_changeable
    add     esp,    4
    
    and     eax,    eax
    jz      .Linstall_done
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Test EFLAGS.ID to determine whether CPUID is available.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    HEX (00200000)
    call    eflags_bit_changeable
    add     esp,    4
    
    mov     dword ptr [cpuid_available], eax
    
    and     eax,    eax
    jz      .Linstall_done

.486

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Read CR4.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ebx,    cr4

.686

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Enable PGE if supported.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    CF_PGE
    call    _cpu_supports
    add     esp,    4
    
    and     eax,    eax
    jz      .Linstall_fxsr
    
    or      ebx,    HEX (00000080)

.Linstall_fxsr:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Enable OSFXSR if supported.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    CF_FXSR
    call    _cpu_supports
    add     esp,    4
    
    and     eax,    eax
    jz      .Linstall_sse
    
    or      ebx,    HEX (00000200)

.Linstall_sse:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Enable OSXMMEXCPT if SSE is supported.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    CF_SSE
    call    _cpu_supports
    add     esp,    4
    
    and     eax,    eax
    jz      .Linstall_write_cr4
    
    or      ebx,    HEX (00000400)

.Linstall_write_cr4:

.486

    mov     cr4,    ebx

.686

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Remember whether RDTSC is available.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    CF_RDTSC
    call    _cpu_supports
    add     esp,    4
    
    mov     dword ptr [supports_rdtsc], eax

.Linstall_done:

.386

    pop     ebx
    mov     esp,    ebp
    pop     ebp
    ret

;******************************************************************************
; @function         _cpu_supports
;
; @param            feature
;******************************************************************************
global      _cpu_supports
_cpu_supports:

    push    ebp
    mov     ebp,    esp
    push    ebx
    
    mov     eax,    dword ptr [ebp + 8]
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; CF_CPUID is handled specially.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cmp     eax,    CF_CPUID
    jne     .Lsupports_feature
    
    mov     eax,    dword ptr [cpuid_available]
    jmp     .Lsupports_done

.Lsupports_feature:

    cmp     dword ptr [cpuid_available], 0
    jne     .Lsupports_cpuid
    
    xor     eax,    eax
    jmp     .Lsupports_done

.Lsupports_cpuid:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; The upper bits select the CPUID result register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ebx,    eax
    and     ebx,    HEX (FFFFFFE0)
    
    push    ebx
    push    1
    call    _cpu_id_get_register
    add     esp,    8
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Extract the feature bit number.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ecx,    dword ptr [ebp + 8]
    sub     ecx,    ebx
    
    shr     eax,    cl
    and     eax,    1

.Lsupports_done:

    pop     ebx
    mov     esp,    ebp
    pop     ebp
    ret

;******************************************************************************
; @function         _cpu_id_get_register
;
; @param            function
; @param            reg
;******************************************************************************
global      _cpu_id_get_register
_cpu_id_get_register:

    push    ebp
    mov     ebp,    esp
    push    ebx
    push    esi
    push    edi
    
    cmp     dword ptr [cpuid_available], 0
    jne     .Lcpuid_available
    
    xor     eax,    eax
    jmp     .Lcpuid_done

.Lcpuid_available:

.686

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Execute CPUID.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    dword ptr [ebp + 8]
    xor     ecx,    ecx
    cpuid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Select the requested result register.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     esi,    dword ptr [ebp + 12]
    
    cmp     esi,    CR_EAX
    je      .Lcpuid_done
    
    cmp     esi,    CR_EBX
    jne     .Lcpuid_ecx
    
    mov     eax,    ebx
    jmp     .Lcpuid_done

.Lcpuid_ecx:

    cmp     esi,    CR_ECX
    jne     .Lcpuid_edx
    
    mov     eax,    ecx
    jmp     .Lcpuid_done

.Lcpuid_edx:

    cmp     esi,    CR_EDX
    jne     .Lcpuid_invalid
    
    mov     eax,    edx
    jmp     .Lcpuid_done

.Lcpuid_invalid:

    xor     eax,    eax

.Lcpuid_done:

.386

    pop     edi
    pop     esi
    pop     ebx
    mov     esp,    ebp
    pop     ebp
    ret

;******************************************************************************
; @function         _cpu_msr_read
;
; @param            msr
;
; @return           edx:eax
;******************************************************************************
global      _cpu_msr_read
_cpu_msr_read:

    push    ebp
    mov     ebp,    esp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Do not execute RDMSR unless the CPU advertises MSR support.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    CF_MSR
    call    _cpu_supports
    add     esp,    4
    
    and     eax,    eax
    jnz     .Lmsr_read
    
    xor     eax,    eax
    xor     edx,    edx
    jmp     .Lmsr_read_done

.Lmsr_read:

.686

    mov     ecx,    dword ptr [ebp + 8]
    rdmsr

.Lmsr_read_done:

.386

    mov     esp,    ebp
    pop     ebp
    ret

;******************************************************************************
; @function         _cpu_msr_write
;
; @param            msr
; @param            value
;******************************************************************************
global      _cpu_msr_write
_cpu_msr_write:

    push    ebp
    mov     ebp,    esp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Do not execute WRMSR unless the CPU advertises MSR support.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    CF_MSR
    call    _cpu_supports
    add     esp,    4
    
    and     eax,    eax
    jz      .Lmsr_write_done

.686

    mov     ecx,    dword ptr [ebp + 8]
    mov     eax,    dword ptr [ebp + 12]
    mov     edx,    dword ptr [ebp + 16]
    
    wrmsr

.Lmsr_write_done:

.386

    mov     esp,    ebp
    pop     ebp
    ret


;******************************************************************************
; @function         _cpu_rdtsc_available
;******************************************************************************
global      _cpu_rdtsc_available
_cpu_rdtsc_available:

    mov     eax,    dword ptr [supports_rdtsc]
    ret

;******************************************************************************
; @function         _cpu_rdtsc
;
; @return           edx:eax
;******************************************************************************
global      _cpu_rdtsc
_cpu_rdtsc:

    cmp     dword ptr [supports_rdtsc], 0
    jne     .Lrdtsc
    
    xor     eax,    eax
    xor     edx,    edx
    ret

.Lrdtsc:

.686

    rdtsc

.386

    ret

;******************************************************************************
; @function         _cpu_has_cpuid
;******************************************************************************
global      _cpu_has_cpuid
_cpu_has_cpuid:

    mov     eax,    dword ptr [cpuid_available]
    ret

;******************************************************************************
; @function         _cpu_has_apic
;******************************************************************************
global      _cpu_has_apic
_cpu_has_apic:

    push    CF_APIC
    call    _cpu_supports
    
    add     esp,    4
    ret

;******************************************************************************
; @function         _cpu_has_pse
;******************************************************************************
global      _cpu_has_pse
_cpu_has_pse:

    push    CF_PSE
    call    _cpu_supports
    
    add     esp,    4
    ret

;******************************************************************************
; @function         _fpu_install
;******************************************************************************
global      _fpu_install
_fpu_install:

.486

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Clear CR0.TS.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    clts

.387

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Read CR0.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    cr0
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Clear CR0.EM and set CR0.MP.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    and     eax,    HEX (FFFFFFFB)
    or      eax,    HEX (00000002)
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Write CR0.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     cr0,    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Initialize the x87 FPU.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    fninit
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return to caller.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ret

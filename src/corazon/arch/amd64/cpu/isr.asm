IRQ     MACRO   index, byte
PUBLIC irq&index
irq&index:
    cli
    push    0x00
    push    byte
    jmp     isr_common
ENDM

ISR_NOERR   MACRO   index
PUBLIC isr&index
isr&index:
    cli
    push    0x00
    push    index
    jmp     isr_common
ENDM

ISR_ERR     MACRO   index
PUBLIC isr&index
isr&index:
    cli
    push    index
    jmp     isr_common
ENDM

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Interrupt Requests.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

IRQ  0, 32
IRQ  1, 33
IRQ  2, 34
IRQ  3, 35
IRQ  4, 36
IRQ  5, 37
IRQ  6, 38
IRQ  7, 39
IRQ  8, 40
IRQ  9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

ISR_NOERR 128

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Fatal signal, stop everything.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
PUBLIC      isr2
isr2:

    cli

.L1:

    hlt
    jmp     .L1

PUBLIC      isr123
isr123:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Disable interrupts.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cli
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Then we can proceed!
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    0
    push    123

PUBLIC      isr_common
isr_common:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save all registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cmp     qword ptr [rsp + 24],   8
    je      isr_common.push
    
    swapgs

isr_common.push:

    push    rax
    push    rbx
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    push    rbp
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15
    
    cld
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Call interrupt handler.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     rbx,    rsp
    mov     rcx,    rsp
    
    and     rsp,    -16
    sub     rsp,    32
    
    call    isr_handler
    mov     rsp,    rbx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore all registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rbp
    pop     rdi
    pop     rsi
    pop     rdx
    pop     rcx
    pop     rbx
    pop     rax
    
    cmp     qword ptr [rsp + 24],   8
    je      isr_common.done
    
    swapgs

isr_common.done:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Cleanup error code and interrupt #.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     rsp,    16
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return from interrupt
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    iretq

EXTERN      g_current_thread:QWORD

EXTERN      handle_syscall:PROC
EXTERN      syscall_stack:QWORD

EXTERN      syscall_user_rcx:QWORD
EXTERN      syscall_user_r11:QWORD
EXTERN      syscall_user_rsp:QWORD
EXTERN      syscall_user_rax:QWORD

PUBLIC      syscall_entry
syscall_entry:

    swapgs
    
    mov     qword ptr [syscall_user_rsp],   rsp
    mov     qword ptr [syscall_user_rax],   rax
    mov     qword ptr [syscall_user_rcx],   rcx
    mov     qword ptr [syscall_user_r11],   r11
    
    mov     rax,    qword ptr [g_current_thread]
    
    and     rax,    rax
    jz      syscall_entry.bail
    
    mov     rsp,    qword ptr [syscall_stack]
    
    push    qword ptr [rax + 160]
    push    qword ptr [syscall_user_rsp]
    push    r11
    push    qword ptr [rax + 152]
    push    rcx
    
    push    0
    push    128
    
    push    qword ptr [syscall_user_rax]
    push    rbx
    push    qword ptr [syscall_user_rcx]
    push    rdx
    push    rsi
    push    rdi
    push    rbp
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15
    
    cld
    
    mov     rbx,    rsp
    mov     rcx,    rsp
    
    and     rsp,    -16
    sub     rsp,    32
    
    call    handle_syscall
    mov     rsp,    rbx
    
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rbp
    pop     rdi
    pop     rsi
    pop     rdx
    pop     rcx
    pop     rbx
    pop     rax
    
    add     rsp,    16
    
    mov     rcx,    qword ptr [rsp]
    mov     r11,    qword ptr [rsp + 16]
    mov     rsp,    qword ptr [rsp + 24]
    
    swapgs
    sysretq
    
syscall_entry.bail:

    mov     rcx,    qword ptr [syscall_user_rcx]
    mov     r11,    qword ptr [syscall_user_r11]
    mov     rsp,    qword ptr [syscall_user_rsp]
    
    swapgs
    sysretq

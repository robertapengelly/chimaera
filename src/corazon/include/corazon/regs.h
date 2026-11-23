/******************************************************************************
 * @file            regs.h
 *****************************************************************************/
#ifndef     _REGS_H
#define     _REGS_H

#include    <stdint.h>

#if     defined (__AMD64__)
__attribute__ ((packed)) struct regs {

    /* Pushed by common stub */
    uintptr_t r15, r14, r13, r12;
    uintptr_t r11, r10, r9, r8;
    uintptr_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    
    /* Pushed by wrapper */
    uintptr_t int_no, err_code;
    
    /* Pushed by interrupt */
    uintptr_t rip, cs, rflags, rsp, ss;

};
#else
__attribute__ ((packed)) struct regs {

    /* Pushed by common stub */
    uintptr_t gs, fs, es, ds;
    uintptr_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    
    /* Pushed by wrapper */
    uintptr_t int_no, err_code;
    
    /* Pushed by interrupt */
    uintptr_t eip, cs, eflags, useresp, ss;

};
#endif

#endif      /* _REGS_H */

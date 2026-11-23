/******************************************************************************
 * @file            pcbios.h
 *****************************************************************************/
#ifndef     _PCBIOS_H
#define     _PCBIOS_H

#include    <stdint.h>

__attribute__ ((packed)) typedef struct {

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    
    uint16_t ds;
    uint16_t es;
    uint16_t fs;
    uint16_t gs;
    
    uint32_t eflags;

} DWORD_REGS;

__attribute__ ((packed)) typedef struct {

    uint16_t ax, _upper_ax;
    uint16_t bx, _upper_bx;
    uint16_t cx, _upper_cx;
    uint16_t dx, _upper_dx;
    
    uint16_t si, _upper_si;
    uint16_t di, _upper_di;
    uint16_t bp, _upper_bp;
    
    uint16_t ds;
    uint16_t es;
    uint16_t fs;
    uint16_t gs;
    
    uint16_t flags, _upper_flags;

} WORD_REGS;

__attribute__ ((packed)) typedef struct {

    uint8_t al, ah;
    uint16_t _upper_ax;
    
    uint8_t bl, bh;
    uint16_t _upper_bx;
    
    uint8_t cl, ch;
    uint16_t _upper_cx;
    
    uint8_t dl, dh;
    uint16_t _upper_dx;
    
    uint16_t si, _upper_si;
    uint16_t di, _upper_di;
    uint16_t bp, _upper_bp;
    
    uint16_t ds;
    uint16_t es;
    uint16_t fs;
    uint16_t gs;
    
    uint16_t flags, _upper_flags;

} BYTE_REGS;

__attribute__ ((packed)) typedef union _regs {

    DWORD_REGS x;
    DWORD_REGS d;
    
    WORD_REGS w;
    BYTE_REGS b;

} REGS;

int int386 (int ivec, REGS *in, REGS *out);

#endif      /* _PCBIOS_H */

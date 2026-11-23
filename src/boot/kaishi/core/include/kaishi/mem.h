/******************************************************************************
 * @file            mem.h
 *****************************************************************************/
#ifndef     _MEM_H
#define     _MEM_H

#include    <stdint.h>

extern unsigned char mem_layout[];
extern uintptr_t mem_start, mem_end, mem_used;

void mem_init (void);

#endif      /* _MEM_H */

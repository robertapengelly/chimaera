/******************************************************************************
 * @file            cga.h
 *****************************************************************************/
#ifndef     _CGA_H
#define     _CGA_H

#include    <stdint.h>

__attribute__ ((packed)) typedef struct cga_info {

    uintptr_t address;
    
    uint32_t width;
    uint32_t height;

} cga_info_t;

int cga_initialize (console_t *console, int need_buffer, const cga_info_t *info);

#endif      /* _CGA_H */

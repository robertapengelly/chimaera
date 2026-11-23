/******************************************************************************
 * @file            ega.h
 *****************************************************************************/
#ifndef     _EGA_H
#define     _EGA_H

#include    <stdint.h>

__attribute__ ((packed)) typedef struct ega_info {

    uintptr_t address;
    
    uint32_t width;
    uint32_t height;

} ega_info_t;

int ega_initialize (console_t *console, int need_buffer, const ega_info_t *info);

#endif      /* _EGA_H */

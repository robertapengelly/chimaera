/******************************************************************************
 * @file            framebuffer.h
 *****************************************************************************/
#ifndef     _FRAMEBUFFER_H
#define     _FRAMEBUFFER_H

#include    <stdint.h>
#include    <stddef.h>

__attribute__ ((packed)) typedef struct framebuffer_info {

    uint64_t address;
    uint64_t size;

    uint32_t width;
    uint32_t height;
    uint32_t pitch;

    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;

} framebuffer_info_t;

int framebuffer_initialize (console_t *console, int need_buffer, framebuffer_info_t *info);

#endif      /* _FRAMEBUFFER_H */

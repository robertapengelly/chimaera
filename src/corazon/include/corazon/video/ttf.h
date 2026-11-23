/******************************************************************************
 * @file            ttf.h
 *****************************************************************************/
#ifndef     _TTF_H
#define     _TTF_H

#include    <stddef.h>
#include    <stdint.h>

extern uint8_t *ttf_font_bitmap;

extern uint32_t ttf_font_width;
extern uint32_t ttf_font_height;
extern uint32_t ttf_font_pitch;

extern int ttf_font_loaded;
int console_font_load_ttf (const void *data, size_t length);

#endif      /* _TTF_H */

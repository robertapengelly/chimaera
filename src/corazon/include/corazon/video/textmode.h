/******************************************************************************
 * @file            textmode.h
 *****************************************************************************/
#ifndef     _TEXTMODE_H
#define     _TEXTMODE_H

#include    <stddef.h>
#include    <stdint.h>

__attribute__ ((packed)) typedef struct textmode_info {

    uintptr_t address;
    
    uint32_t width;
    uint32_t height;

} textmode_info_t;

void textmode_initialize (const textmode_info_t *info);

void textmode_clear (void);
void textmode_putchar (char ch);
void textmode_puts (const char *str);

void textmode_set_colour (uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg, uint8_t bb);

uint32_t textmode_get_width (void);
uint32_t textmode_get_height (void);

void textmode_move_cursor (uint32_t x, uint32_t y);

#endif  /* _TEXTMODE_H */

/******************************************************************************
 * @file            console.h
 *****************************************************************************/
#ifndef     _CONSOLE_H
#define     _CONSOLE_H

#include    <corazon/video/cga.h>
#include    <corazon/video/ega.h>
#include    <corazon/video/framebuffer.h>
#include    <corazon/video/textmode.h>

#include    <stddef.h>
#include    <stdint.h>
#include    <termios.h>

#define     CONSOLE_TYPE_TEXTMODE       0
#define     CONSOLE_TYPE_CGA            1
#define     CONSOLE_TYPE_EGA            2
#define     CONSOLE_TYPE_FRAMEBUFFER    3

#define     CONSOLE_CURSOR_HEIGHT       2

__attribute__ ((packed)) typedef struct console {

    int active;
    
    uint32_t cursor_x;
    uint32_t cursor_y;
    
    uint32_t default_foreground;
    uint32_t default_background;
    
    uint32_t foreground;
    uint32_t background;
    
    uint8_t *screen_buffer;
    uint8_t *buffer;
    
    size_t buffer_size;
    
    int alternate_screen;
    uint32_t usable_height;
    
    uint32_t saved_cursor_x;
    uint32_t saved_cursor_y;
    uint32_t saved_foreground;
    uint32_t saved_background;
    
    uint8_t saved_ansi_foreground;
    
    int saved_ansi_bold;
    int saved_reverse_video;
    int saved_cursor_enabled;
    
    void (*move_cursor) (struct console *console, uint32_t x, uint32_t y);
    
    void (*clear) (struct console *console);
    void (*putchar) (struct console *console, char ch);
    
    uint32_t (*get_width) (void);
    uint32_t (*get_height) (void);
    
    uint32_t (*make_colour) (uint8_t r, uint8_t g, uint8_t b);
    void (*refresh) (console_t *console);
    
    size_t input_capacity;
    char *input;
    
    uint32_t input_head;
    uint32_t input_count;
    uint32_t input_ready;
    
    uint32_t input_uncommitted;
    uint32_t input_echoed;
    
    struct termios termios;
    struct thread *input_reader;
    
    uintptr_t input_address;
    size_t input_length;
    
    uint32_t cursor_saved[CONSOLE_CURSOR_HEIGHT * 8];
    
    int cursor_visible;
    int cursor_enabled;
    int reverse_video;
    
    uint8_t ansi_foreground;
    int ansi_bold;
    
    uint32_t cell_width;
    uint32_t cell_height;
    
    uint32_t escape_parameters[16];
    
    uint8_t escape_state;
    uint8_t escape_private;
    uint8_t escape_count;
    
    void (*erase_line) (struct console *console, uint32_t row, uint32_t first, uint32_t last);
    void (*cursor) (console_t *console, int show);

} console_t;

__attribute__ ((packed)) typedef struct console_info {

    uint32_t type;
    
    union {

        textmode_info_t textmode;
        
        cga_info_t cga;
        ega_info_t ega;
        
        framebuffer_info_t framebuffer;
    
    };

} console_info_t;

void console_initialize (const console_info_t *info);
void console_move_cursor (uint32_t x, uint32_t y);

void console_input_char (char ch);
void console_put_char (char ch);
void console_clear (void);
void console_puts (const char *str);

uint32_t console_get_width (void);
uint32_t console_get_height (void);

uint32_t console_get_pixel_height (void);
uint32_t console_get_pixel_width (void);

uint32_t console_get_type (void);
void console_font_activate (void);

void console_setbackgound (uint8_t r, uint8_t g, uint8_t b);
void console_setforegound (uint8_t r, uint8_t g, uint8_t b);

void console_switch (console_t *console);

console_t *console_get_current (void);
console_t *console_create (void);

#endif      /* _CONSOLE_H */

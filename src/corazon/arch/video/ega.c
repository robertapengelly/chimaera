/******************************************************************************
 * @file            ega.c
 *****************************************************************************/
#include    <corazon/utils/misc.h>

#include    <corazon/console.h>
#include    <corazon/video/ega.h>
#include    <corazon/video/ttf.h>

#include    <stddef.h>
#include    <stdint.h>

/**
 * 8x8 bitmap font.
 *
 * Each character consists of 8 bytes.
 * Each byte represents one row.
 *
 * Bit 7 = left-most pixel.
 * Bit 0 = right-most pixel.
 */
extern const uint8_t font8x8[256][8];

#define     FONT_HEIGHT                 (ttf_font_loaded ? ttf_font_height : 8)
#define     FONT_WIDTH                  (ttf_font_loaded ? ttf_font_width : 8)

#define     EGA_SEQ_INDEX               0x3C4
#define     EGA_SEQ_DATA                0x3C5
#define     EGA_GC_INDEX                0x3CE
#define     EGA_GC_DATA                 0x3CF

#define     EGA_SEQ_MAP_MASK            0x02
#define     EGA_GC_READ_MAP_SELECT      0x04

static volatile uint8_t *buffer = 0;

static uint32_t width = 0;
static uint32_t height = 0;

static uint32_t bytes_per_line = 0;

/******************************************************************************
 * Write a byte to an I/O port.
 *****************************************************************************/
static void outb (uint16_t port, uint8_t value) {
    __asm__ volatile ( "outb %0, %1" : : "a" (value), "Nd" (port) );
}

/******************************************************************************
 * Select which EGA plane is writable.
 *****************************************************************************/
static void select_write_plane (uint8_t plane) {

    outb (EGA_SEQ_INDEX, EGA_SEQ_MAP_MASK);
    outb (EGA_SEQ_DATA, (uint8_t) (1U << plane));

}

/******************************************************************************
 * Select which EGA plane is visible to CPU reads.
 *****************************************************************************/
static void select_read_plane (uint8_t plane) {

    outb (EGA_GC_INDEX, EGA_GC_READ_MAP_SELECT);
    outb (EGA_GC_DATA, plane);

}

static size_t plane_size (void) {
    return (size_t) bytes_per_line * height;
}

/******************************************************************************
 * Write one pixel.
 *
 * EGA mode 0x10 is planar. Each byte describes eight horizontal pixels in one
 * plane. The four corresponding plane bits together form the 4-bit colour.
 *****************************************************************************/
static void putpixel (console_t *console, uint32_t x, uint32_t y, uint8_t colour) {

    volatile uint8_t *pixel;
    
    uint8_t mask;
    uint8_t value;
    uint8_t plane;
    
    if (x >= width || y >= height) {
        return;
    }
    
    if (console->buffer) {
    
        mask = (uint8_t) (0x80 >> (x & 7));
        
        for (plane = 0; plane < 4; plane++) {
        
            pixel = console->buffer + ((size_t) plane * plane_size ()) + ((size_t) y * bytes_per_line) + (x >> 3);
            value = *pixel;
            
            if (colour & (1U << plane)) {
                value |= mask;
            } else {
                value &= (uint8_t) ~mask;
            }
            
            *pixel = value;
        
        }
    
    }
    
    if (console->active) {
    
        pixel = buffer + ((size_t) y * bytes_per_line) + (x >> 3);
        mask = (uint8_t) (0x80 >> (x & 7));
        
        for (plane = 0; plane < 4; plane++) {
        
            select_read_plane (plane);
            select_write_plane (plane);
            
            value = *pixel;
            
            if (colour & (1U << plane)) {
                value |= mask;
            } else {
                value &= (uint8_t) ~mask;
            }
            
            *pixel = value;
        
        }
    
    }

}

/******************************************************************************
 * Draw one character at a pixel coordinate.
 *****************************************************************************/
static void draw_char (console_t *console, uint32_t x, uint32_t y, unsigned char ch) {

    uint32_t column, row;
    
    const uint8_t *glyph = 0;
    uint8_t bits;
    
    if (ttf_font_loaded) {
        glyph = ttf_font_bitmap + (size_t) ch * ttf_font_pitch * FONT_HEIGHT;
    }
    
    for (row = 0; row < FONT_HEIGHT; row++) {
    
        for (column = 0; column < FONT_HEIGHT; column++) {
        
            bits = glyph ? glyph[(size_t) row * ttf_font_pitch + (column >> 3)] : font8x8[ch][row];
            
            if (bits & (0x80 >> (column & 7))) {
                putpixel (console, x + column, y + row, console->foreground);
            } else {
                putpixel (console, x + column, y + row, console->background);
            }
        
        }
    
    }

}

/******************************************************************************
 * Clear one EGA graphics scanline.
 *****************************************************************************/
static void clear_scanline (console_t *console, uint32_t y) {

    volatile uint8_t *line;
    
    uint8_t plane, value;
    uint32_t x;
    
    for (x = 0; x < bytes_per_line; x++) {
    
        for (plane = 0; plane < 4; plane++) {
        
            line = console->buffer + ((size_t) plane * plane_size ()) + ((size_t) y * bytes_per_line);
            value = (console->background & (1U << plane)) ? 0xFF : 0x00;
            
            line[x] = value;
        
        }
    
    }

}

/******************************************************************************
 * Scroll the display upward by one character row.
 *****************************************************************************/
static void scroll (console_t *console) {

    volatile uint8_t *destination;
    volatile uint8_t *source;
    
    uint32_t x;
    uint32_t y;
    
    uint8_t plane;
    
    for (plane = 0; plane < 4; plane++) {
    
        for (y = 0; y < console->usable_height - FONT_HEIGHT; y++) {
        
            destination = console->buffer + ((size_t) plane * plane_size ()) + ((size_t) y * bytes_per_line);
            source = console->buffer + ((size_t) plane * plane_size ()) + ((size_t) (y + FONT_HEIGHT) * bytes_per_line);
            
            for (x = 0; x < bytes_per_line; x++) {
                destination[x] = source[x];
            }
        
        }
    
    }
    
    for (y = console->usable_height - FONT_HEIGHT; y < console->usable_height; y++) {
        clear_scanline (console, y);
    }
    
    if (console->active) {
    
        for (y = 0; y < console->usable_height; y++) {
        
            destination = buffer + ((size_t) y * bytes_per_line);
            
            for (x = 0; x < bytes_per_line; x++) {
            
                for (plane = 0; plane < 4; plane++) {
                
                    select_read_plane (plane);
                    select_write_plane (plane);
                    
                    source = console->buffer + ((size_t) plane * plane_size ()) + ((size_t) y * bytes_per_line);
                    destination[x] = source[x];
                
                }
            
            }
        
        }
    
    }

}

/******************************************************************************
 * Move to the beginning of the next line.
 *****************************************************************************/
static void newline (console_t *console) {

    console->cursor_x = 0;
    console->cursor_y += FONT_HEIGHT;
    
    if (console->cursor_y + FONT_HEIGHT > console->usable_height) {
    
        scroll (console);
        console->cursor_y = console->usable_height - FONT_HEIGHT;
    
    }

}

/******************************************************************************
 * Convert an RGB colour to the nearest EGA colour.
 *****************************************************************************/
static uint32_t ega_rgb_to_colour (uint8_t r, uint8_t g, uint8_t b) {

    static const uint8_t ega_palette[16][3] = {
    
        {   0,   0,   0 },
        {   0,   0, 170 },
        {   0, 170,   0 },
        {   0, 170, 170 },
        { 170,   0,   0 },
        { 170,   0, 170 },
        { 170,  85,   0 },
        { 170, 170, 170 },
        {  85,  85,  85 },
        {  85,  85, 255 },
        {  85, 255,  85 },
        {  85, 255, 255 },
        { 255,  85,  85 },
        { 255,  85, 255 },
        { 255, 255,  85 },
        { 255, 255, 255 }
    
    };
    
    uint32_t best_distance = UINT32_MAX;
    
    uint8_t best_colour = 0;
    uint8_t i;
    
    for (i = 0; i < 16; i++) {
    
        int32_t dr = (int32_t) r - ega_palette[i][0];
        int32_t dg = (int32_t) g - ega_palette[i][1];
        int32_t db = (int32_t) b - ega_palette[i][2];
        
        uint32_t distance = (uint32_t) ((dr * dr) + (dg * dg) + (db * db));
        
        if (distance < best_distance) {
        
            best_distance = distance;
            best_colour = i;
        
        }
    
    }
    
    return best_colour;

}

/******************************************************************************
 * Clear the complete EGA graphics screen.
 *****************************************************************************/
static void ega_clear (console_t *console) {

    uint32_t y;
    
    for (y = 0; y < height; y++) {
        clear_scanline (console, y);
    }
    
    console->cursor_x = 0;
    console->cursor_y = 0;

}

/******************************************************************************
 * Print one character.
 *****************************************************************************/
static void ega_putchar (console_t *console, char ch) {

    switch (ch) {
    
        case '\n':
        
            newline (console);
            break;
        
        case '\r':
        
            console->cursor_x = 0;
            break;
        
        case '\t':
        
            do {
                ega_putchar (console, ' ');
            } while ((console->cursor_x / FONT_WIDTH) % 4);
            
            break;
        
        case '\b':
        
            if (console->cursor_x >= FONT_WIDTH) {
            
                console->cursor_x -= FONT_WIDTH;
                draw_char (console, console->cursor_x, console->cursor_y, ' ');
            
            }
            
            break;
        
        default:
        
            if (console->cursor_x + FONT_WIDTH > width) {
                newline (console);
            }
            
            draw_char (console, console->cursor_x, console->cursor_y, (unsigned char) ch);
            console->cursor_x += FONT_WIDTH;
            
            break;
    
    }

}

/* Erase a range of character cells without moving the cursor. */
static void ega_erase_line (console_t *console, uint32_t row, uint32_t first, uint32_t last) {

    uint32_t column;
    
    for (column = first; column <= last; column++) {
        draw_char (console, column * FONT_WIDTH, row * FONT_HEIGHT, ' ');
    }

}

/******************************************************************************
 * Print a null-terminated string.
 *****************************************************************************/
/*static void ega_puts (console_t *console, const char *str) {

    while (*str != '\0') {
    
        ega_putchar (console, *str);
        str++;
    
    }

}*/

/******************************************************************************
 * Return width.
 *****************************************************************************/
static uint32_t ega_get_width (void) {
    return width / FONT_WIDTH;
}

/******************************************************************************
 * Return height.
 *****************************************************************************/
static uint32_t ega_get_height (void) {
    return height / FONT_HEIGHT;
}

/******************************************************************************
 * Set cursor position.
 *****************************************************************************/
static void ega_move_cursor (console_t *console, uint32_t x, uint32_t y) {

    console->cursor_x = x * FONT_WIDTH;
    console->cursor_y = y * FONT_HEIGHT;

}

static void ega_cursor (console_t *console, int show) {

    volatile uint8_t *pixel;
    
    size_t offset;
    uint8_t plane;
    
    uint32_t row, col;
    
    if (!console->active || console->cursor_x + FONT_WIDTH > width || console->cursor_y + FONT_HEIGHT > console->usable_height) {
        return;
    }
    
    if (ttf_font_loaded) {
    
        if (!console->buffer) {
            return;
        }
        
        for (plane = 0; plane < 4; plane++) {
        
            select_read_plane (plane);
            select_write_plane (plane);
            
            for (row = 0; row < CONSOLE_CURSOR_HEIGHT; row++) {
            
                uint32_t y = console->cursor_y + FONT_HEIGHT - CONSOLE_CURSOR_HEIGHT + row;
                
                for (col = 0; col < FONT_WIDTH; col++) {
                
                    uint32_t x = console->cursor_x + col;
                    
                    uint8_t mask = (uint8_t) (0x80 >> (x & 7));
                    uint8_t value;
                    
                    offset = (size_t) y * bytes_per_line + (x >> 3);
                    pixel = buffer + offset;
                    
                    value = show ? (console->foreground & (1U << plane) ? mask : 0) :
                        (console->buffer[(size_t) plane * plane_size () + offset] & mask);
                    
                    *pixel = (uint8_t) ((*pixel & (uint8_t) ~mask) | value);
                
                }
            
            }
        
        }
        
        return;
    
    }
    
    for (plane = 0; plane < 4; plane++) {
    
        select_read_plane (plane);
        select_write_plane (plane);
        
        for (row = 0; row < CONSOLE_CURSOR_HEIGHT; row++) {
        
            offset = (size_t) (console->cursor_y + FONT_HEIGHT - CONSOLE_CURSOR_HEIGHT + row) * bytes_per_line + (console->cursor_x >> 3);
            pixel = buffer + offset;
            
            if (show) {
            
                console->cursor_saved[plane * CONSOLE_CURSOR_HEIGHT + row] = *pixel;
                *pixel = (console->foreground & (1U << plane)) ? 0xFF : 0x00;
            
            } else {
                *pixel = (uint8_t) console->cursor_saved[plane * CONSOLE_CURSOR_HEIGHT + row];
            }
        
        }
    
    }

}

/****************************************************************************
 * Refresh the screen so that the contents are updated.
 ****************************************************************************/
static void ega_refresh (console_t *console) {

    volatile uint8_t *destination;
    uint8_t *source;
    
    uint8_t plane;
    size_t i;
    
    if (console->active) {
    
        destination = buffer;
        
        for (i = 0; i < plane_size (); i++) {
        
            for (plane = 0; plane < 4; plane++) {
            
                select_read_plane (plane);
                select_write_plane (plane);
                
                source = console->buffer + ((size_t) plane * plane_size ());
                destination[i] = source[i];
            
            }
        
        }
    
    }

}

/******************************************************************************
 * Initialize EGA graphics output.
 *****************************************************************************/
int ega_initialize (console_t *console, int need_buffer, const ega_info_t *info) {

    buffer = (volatile uint8_t *) (uintptr_t) info->address;
    
    width = info->width;
    height = info->height;
    
    console->usable_height = height - (height % FONT_HEIGHT);
    bytes_per_line = width / 8;
    
    console->cursor_x = 0;
    console->cursor_y = 0;
    
    console->foreground = 0x07;
    console->background = 0x00;
    
    if (need_buffer) {
    
        console->buffer_size = plane_size () * 4;
        
        if (!(console->buffer = kmalloc (console->buffer_size))) {
            return 1;
        }
    
    }
    
    ega_clear (console);
    ega_refresh (console);
    
    console->erase_line = ega_erase_line;
    console->cell_width = FONT_WIDTH;
    console->cell_height = FONT_HEIGHT;
    
    console->move_cursor = ega_move_cursor;
    console->cursor = ega_cursor;
    
    console->clear = ega_clear;
    console->putchar = ega_putchar;
    
    console->get_width = ega_get_width;
    console->get_height = ega_get_height;
    
    console->make_colour = ega_rgb_to_colour;
    console->refresh = ega_refresh;
    
    return 0;

}

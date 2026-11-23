/******************************************************************************
 * @file            framebuffer.c
 *****************************************************************************/
#include    <corazon/utils/misc.h>

#include    <corazon/console.h>
#include    <corazon/video/ttf.h>

#include    <corazon/video/framebuffer.h>

#include    <stddef.h>
#include    <stdint.h>

extern const uint8_t font8x14[256][14];
extern const uint8_t font8x16[256][16];

#define     FONT_HEIGHT                 (ttf_font_loaded ? ttf_font_height : 16)
#define     FONT_WIDTH                  (ttf_font_loaded ? ttf_font_width : 8)

static framebuffer_info_t framebuffer = { 0 };

/******************************************************************************
 * Convert an RGB colour into the actual framebuffer pixel format.
 *****************************************************************************/
static uint32_t framebuffer_colour (uint8_t red, uint8_t green, uint8_t blue) {

    uint32_t colour = 0;
    
    /**
     * This simple implementation assumes 8-bit RGB masks, which covers
     * normal GOP RGB/BGR framebuffer modes.
     */
    if (framebuffer.red_mask == 0x000000FF) {
        colour |= (uint32_t) red;
    } else if (framebuffer.red_mask == 0x00FF0000) {
        colour |= (uint32_t) red << 16;
    }
    
    if (framebuffer.green_mask == 0x0000FF00) {
        colour |= (uint32_t) green << 8;
    }
    
    if (framebuffer.blue_mask == 0x000000FF) {
        colour |= (uint32_t) blue;
    } else if (framebuffer.blue_mask == 0x00FF0000) {
        colour |= (uint32_t) blue << 16;
    }
    
    return colour;

}

/******************************************************************************
 * Write one pixel.
 *****************************************************************************/
static void framebuffer_putpixel (console_t *console, uint32_t x, uint32_t y, uint32_t colour) {

    uint8_t *base;
    uint32_t *pixel;
    
    if (x >= framebuffer.width) {
        return;
    }
    
    if (y >= framebuffer.height) {
        return;
    }
    
    if (console->buffer) {
    
        pixel = (uint32_t *) (console->buffer + ((size_t) y * framebuffer.pitch) + ((size_t) x * sizeof (uint32_t)));
        *pixel = colour;
    
    }
    
    if (console->active) {
    
        base = (uint8_t *) (uintptr_t) framebuffer.address;
        
        pixel = (uint32_t *) (base + ((size_t) y * framebuffer.pitch) + ((size_t) x * sizeof (uint32_t)));
        *pixel = colour;
    
    }

}

/******************************************************************************
 * Draw one character at a pixel coordinate.
 *****************************************************************************/
static void framebuffer_draw_char (console_t *console, uint32_t x, uint32_t y, unsigned char ch) {

    uint32_t column, row;
    
    const uint8_t *glyph = 0;
    uint8_t bits;
    
    if (ttf_font_loaded) {
        glyph = ttf_font_bitmap + (size_t) ch * ttf_font_pitch * FONT_HEIGHT;
    }
    
    for (row = 0; row < FONT_HEIGHT; row++) {
    
        for (column = 0; column < FONT_WIDTH; column++) {
        
            bits = glyph ? glyph[(size_t) row * ttf_font_pitch + (column >> 3)] :
                (FONT_HEIGHT == 16 ? font8x16[ch][row] : font8x14[ch][row]);
            
            if (bits & (0x80 >> (column & 7))) {
                framebuffer_putpixel (console, x + column, y + row, console->foreground);
            } else {
                framebuffer_putpixel (console, x + column, y + row, console->background);
            }
        
        }
    
    }

}

/* Erase a range of character cells without moving the cursor. */
static void framebuffer_erase_line (console_t *console, uint32_t row, uint32_t first, uint32_t last) {

    uint32_t column;
    
    for (column = first; column <= last; column++) {
        framebuffer_draw_char (console, column * FONT_WIDTH, row * FONT_HEIGHT, ' ');
    }

}

/******************************************************************************
 * Scroll framebuffer upward by one character row.
 *****************************************************************************/
static void framebuffer_scroll (console_t *console) {

    size_t offset = (size_t) framebuffer.pitch * FONT_HEIGHT;
    
    uint32_t *destination;
    uint32_t *source;
    
    uint32_t x;
    uint32_t y;
    
    uint8_t *base;
    
    for (y = 0; y < console->usable_height - FONT_HEIGHT; ++y) {
    
        destination = (uint32_t *) (console->buffer + ((size_t) y * framebuffer.pitch));
        source = (uint32_t *) (console->buffer + ((size_t) y * framebuffer.pitch) + offset);
        
        for (x = 0; x < framebuffer.width; ++x) {
            destination[x] = source[x];
        }
    
    }
    
    for (y = console->usable_height - FONT_HEIGHT; y < console->usable_height; ++y) {
    
        destination = (uint32_t *) (console->buffer + ((size_t) y * framebuffer.pitch));
        
        for (x = 0; x < framebuffer.width; ++x) {
            destination[x] = console->background;
        }
    
    }
    
    if (console->active) {
    
        base = (uint8_t *) (uintptr_t) framebuffer.address;
        
        for (y = 0; y < console->usable_height; ++y) {
        
            destination = (uint32_t *) (base + ((size_t) y * framebuffer.pitch));
            source = (uint32_t *) (console->buffer + ((size_t) y * framebuffer.pitch));
            
            for (x = 0; x < framebuffer.width; ++x) {
                destination[x] = source[x];
            }
        
        }
    
    }

}

/******************************************************************************
 * Advance to next line.
 *****************************************************************************/
static void framebuffer_newline (console_t *console) {

    console->cursor_x = 0;
    console->cursor_y += FONT_HEIGHT;
    
    if (console->cursor_y + FONT_HEIGHT > console->usable_height) {
        
        framebuffer_scroll (console);
        console->cursor_y = console->usable_height - FONT_HEIGHT;
    
    }

}

/******************************************************************************
 * Clear framebuffer.
 *****************************************************************************/
static void framebuffer_clear (console_t *console) {

    uint32_t x;
    uint32_t y;
    
    for (y = 0; y < framebuffer.height; y++) {
    
        for (x = 0; x < framebuffer.width; x++) {
            framebuffer_putpixel (console, x, y, console->background);
        }
    
    }
    
    console->cursor_x = 0;
    console->cursor_y = 0;

}

/******************************************************************************
 * Print one character.
 *****************************************************************************/
static void framebuffer_putchar (console_t *console, char ch) {

    switch (ch) {
    
        case '\n':
        
            framebuffer_newline (console);
            break;
        
        case '\r':
        
            console->cursor_x = 0;
            break;
        
        case '\t':
        
            /* Simple four-character tab. */
            do {
                framebuffer_putchar (console, ' ');
            } while ((console->cursor_x / FONT_WIDTH) % 4);
            
            break;
        
        case '\b':
        
            if (console->cursor_x >= FONT_WIDTH) {
            
                console->cursor_x -= FONT_WIDTH;
                framebuffer_draw_char (console, console->cursor_x, console->cursor_y, ' ');
            
            }
            
            break;
        
        default:
        
            if (console->cursor_x + FONT_WIDTH > framebuffer.width) {
                framebuffer_newline (console);
            }
            
            framebuffer_draw_char (console, console->cursor_x, console->cursor_y, (unsigned char) ch);
            console->cursor_x += FONT_WIDTH;
            
            break;
    
    }

}

/******************************************************************************
 * Print a string.
 *****************************************************************************/
/*static void framebuffer_puts (console_t *console, const char *str) {

    while (*str) {
    
        framebuffer_putchar (console, *str);
        str++;
    
    }

}*/

/******************************************************************************
 * Return width.
 *****************************************************************************/
static uint32_t framebuffer_get_width (void) {
    return framebuffer.width / FONT_WIDTH;
}

/******************************************************************************
 * Return height.
 *****************************************************************************/
static uint32_t framebuffer_get_height (void) {
    return framebuffer.height / FONT_HEIGHT;
}

/******************************************************************************
 * Set cursor position.
 *****************************************************************************/
static void framebuffer_move_cursor (console_t *console, uint32_t x, uint32_t y) {

    console->cursor_x = x * FONT_WIDTH;
    console->cursor_y = y * FONT_HEIGHT;

}

static void framebuffer_cursor (console_t *console, int show) {

    uint32_t *pixel, *saved_pixel, x, row;
    uint8_t *base;
    
    if (!console->active || !console->buffer || console->cursor_x + FONT_WIDTH > framebuffer.width || console->cursor_y + FONT_HEIGHT > console->usable_height) {
        return;
    }
    
    base = (uint8_t *) (uintptr_t) framebuffer.address;
    
    for (row = 0; row < CONSOLE_CURSOR_HEIGHT; row++) {
    
        pixel = (uint32_t *) (base + ((size_t) (console->cursor_y + FONT_HEIGHT - CONSOLE_CURSOR_HEIGHT + row) * framebuffer.pitch) + ((size_t) console->cursor_x * sizeof (uint32_t)));
        saved_pixel = (uint32_t *) (console->buffer + ((size_t) (console->cursor_y + FONT_HEIGHT - CONSOLE_CURSOR_HEIGHT + row) * framebuffer.pitch) + ((size_t) console->cursor_x * sizeof (uint32_t)));
        
        for (x = 0; x < FONT_WIDTH; x++) {
        
            if (show) {
                pixel[x] = console->foreground;
            } else {
                pixel[x] = saved_pixel[x];
            }
        
        }
    
    }

}

/****************************************************************************
 * Refresh the screen so that the contents are updated.
 ****************************************************************************/
static void framebuffer_refresh (console_t *console) {

    uint8_t *source, *destination;
    size_t i;
    
    if (console->active) {
    
        source = (uint8_t *) (uintptr_t) console->buffer;
        destination = (uint8_t *) (uintptr_t) framebuffer.address;
        
        for (i = 0; i < (size_t) framebuffer.pitch * framebuffer.height; ++i) {
            destination[i] = source[i];
        }
    
    }

}

/******************************************************************************
 * Initialize framebuffer console.
 *****************************************************************************/
int framebuffer_initialize (console_t *console, int need_buffer, framebuffer_info_t *info) {

    framebuffer.address       = info->address;
    framebuffer.size          = info->size;

    framebuffer.width         = info->width;
    framebuffer.height        = info->height;
    framebuffer.pitch         = info->pitch;

    framebuffer.red_mask      = info->red_mask;
    framebuffer.green_mask    = info->green_mask;
    framebuffer.blue_mask     = info->blue_mask;
    framebuffer.reserved_mask = info->reserved_mask;
    
    console->cursor_x = 0;
    console->cursor_y = 0;

    console->foreground = framebuffer_colour (170, 170, 170);
    console->background = framebuffer_colour (0, 0, 0);
    
    console->usable_height = framebuffer.height - (framebuffer.height % FONT_HEIGHT);
    
    if (need_buffer) {
    
        console->buffer_size = (size_t) framebuffer.pitch * framebuffer.height;
        
        if (!(console->buffer = kmalloc (console->buffer_size))) {
            return 1;
        }
    
    }
    
    framebuffer_clear (console);
    framebuffer_refresh (console);
    
    console->erase_line = framebuffer_erase_line;
    console->cell_width = FONT_WIDTH;
    console->cell_height = FONT_HEIGHT;
    
    console->move_cursor = framebuffer_move_cursor;
    console->cursor = framebuffer_cursor;
    
    console->clear = framebuffer_clear;
    console->putchar = framebuffer_putchar;
    
    console->get_width = framebuffer_get_width;
    console->get_height = framebuffer_get_height;
    
    console->make_colour = framebuffer_colour;
    console->refresh = framebuffer_refresh;
    
    return 0;

}

/******************************************************************************
 * @file            cga.c
 *****************************************************************************/
#include    <corazon/utils/misc.h>

#include    <corazon/console.h>
#include    <corazon/video/cga.h>
#include    <corazon/video/ttf.h>

#include    <stddef.h>
#include    <stdint.h>

/**
 * 4x8 bitmap font.
 *
 * Each character consists of 8 bytes.
 * Each byte represents one 4-pixel row in bits 7-4.
 *
 * Bit 7 = left-most pixel.
 * Bit 4 = right-most pixel.
 */
extern const uint8_t font4x8[256][8];
#define     CGA_BUFFER_SIZE             0x4000

#define     FONT_HEIGHT                 (ttf_font_loaded ? ttf_font_height : 8)
#define     FONT_WIDTH                  (ttf_font_loaded ? ttf_font_width : 4)

static volatile uint8_t *buffer = 0;

static uint32_t width = 0;
static uint32_t height = 0;

static uint32_t bytes_per_line = 0;

/****************************************************************************
 * Return the CGA memory offset for one graphics scanline.
 *
 * CGA modes 4 and 5 store even and odd scanlines in separate 8 KiB banks:
 *
 *     0x0000  scanlines 0, 2, 4, ...
 *     0x2000  scanlines 1, 3, 5, ...
 ****************************************************************************/
static size_t scanline_offset (uint32_t y) {

    size_t offset = (size_t) (y >> 1) * bytes_per_line;
    
    if (y & 1) {
        offset += 0x2000;
    }
    
    return offset;

}

/****************************************************************************
 * Write one pixel.
 *
 * CGA mode 4 stores four 2-bit pixels in each byte:
 *
 *     bits 7-6  pixel 0
 *     bits 5-4  pixel 1
 *     bits 3-2  pixel 2
 *     bits 1-0  pixel 3
 ****************************************************************************/
static void putpixel (console_t *console, uint32_t x, uint32_t y, uint8_t value) {

    volatile uint8_t *pixel;
    
    uint8_t shift;
    uint8_t mask;
    uint8_t data;
    
    if (x >= width || y >= height) {
        return;
    }
    
    if (console->buffer) {
    
        pixel = console->buffer + scanline_offset (y) + (x >> 2);
        
        shift = (uint8_t) ((3 - (x & 3)) << 1);
        mask = (uint8_t) (0x03 << shift);
        data = (uint8_t) ((value & 0x03) << shift);
        
        *pixel = (uint8_t) ((*pixel & (uint8_t) ~mask) | data);
    
    }
    
    if (console->active) {
    
        pixel = buffer + scanline_offset (y) + (x >> 2);
        shift = (uint8_t) ((3 - (x & 3)) << 1);
        
        mask = (uint8_t) (0x03 << shift);
        data = (uint8_t) ((value & 0x03) << shift);
        
        *pixel = (uint8_t) ((*pixel & (uint8_t) ~mask) | data);
    
    }

}

/****************************************************************************
 * Draw one character at a pixel coordinate.
 ****************************************************************************/
static void draw_char (console_t *console, uint32_t x, uint32_t y, unsigned char ch) {

    uint32_t column, row;
    
    const uint8_t *glyph = 0;
    uint8_t bits;
    
    if (ttf_font_loaded) {
        glyph = ttf_font_bitmap + (size_t) ch * ttf_font_pitch * FONT_HEIGHT;
    }
    
    for (row = 0; row < FONT_HEIGHT; row++) {
    
        for (column = 0; column < FONT_WIDTH; column++) {
        
            bits = glyph ? glyph[(size_t) row * ttf_font_pitch + (column >> 3)] : font4x8[ch][row];
            
            if (bits & (0x80 >> (column & 7))) {
                putpixel (console, x + column, y + row, console->foreground);
            } else {
                putpixel (console, x + column, y + row, console->background);
            }
        
        }
    
    }

}

/****************************************************************************
 * Clear one graphics scanline.
 ****************************************************************************/
static void clear_scanline (console_t *console, uint32_t y) {

    volatile uint8_t *line;
    
    uint32_t x;
    uint8_t value;
    
    line = console->buffer + scanline_offset (y);

    /* Repeat the 2-bit console->background colour across all four pixels. */
    value = (uint8_t) ((console->background & 0x03) * 0x55);
    
    for (x = 0; x < bytes_per_line; x++) {
        line[x] = value;
    }

}

/****************************************************************************
 * Scroll the display upward by one character row.
 ****************************************************************************/
static void scroll (console_t *console) {

    volatile uint8_t *destination;
    volatile uint8_t *source;
    
    uint32_t x;
    uint32_t y;
    
    for (y = 0; y < console->usable_height - FONT_HEIGHT; y++) {
    
        destination = console->buffer + scanline_offset (y);
        source = console->buffer + scanline_offset (y + FONT_HEIGHT);
        
        for (x = 0; x < bytes_per_line; x++) {
            destination[x] = source[x];
        }
    
    }
    
    for (; y < console->usable_height; y++) {
        clear_scanline (console, y);
    }
    
    if (console->active) {
    
        for (y = 0; y < console->usable_height; y++) {
        
            destination = buffer + scanline_offset (y);
            source = console->buffer + scanline_offset (y);
            
            for (x = 0; x < bytes_per_line; x++) {
                destination[x] = source[x];
            }
        
        }
    
    }

}

/****************************************************************************
 * Move to the beginning of the next line.
 ****************************************************************************/
static void newline (console_t *console) {

    console->cursor_x = 0;
    console->cursor_y += FONT_HEIGHT;
    
    if (console->cursor_y + FONT_HEIGHT > console->usable_height) {
    
        scroll (console);
        console->cursor_y = console->usable_height - FONT_HEIGHT;
    
    }

}

/******************************************************************************
 * Convert an RGB colour to the nearest CGA colour.
 *****************************************************************************/
static uint32_t cga_rgb_to_colour (uint8_t r, uint8_t g, uint8_t b) {

    static const uint8_t cga_palette[4][3] = {
    
        {   0,   0,   0 },
        {   0, 170, 170 },
        { 170,   0, 170 },
        { 170, 170, 170 }
    
    };
    
    uint32_t best_distance = UINT32_MAX;
    
    uint8_t best_colour = 0;
    uint8_t i;
    
    for (i = 0; i < 4; i++) {
    
        int32_t dr = (int32_t) r - cga_palette[i][0];
        int32_t dg = (int32_t) g - cga_palette[i][1];
        int32_t db = (int32_t) b - cga_palette[i][2];
        
        uint32_t distance = (uint32_t) ((dr * dr) + (dg * dg) + (db * db));
        
        if (distance < best_distance) {
        
            best_distance = distance;
            best_colour = i;
        
        }
    
    }
    
    return best_colour;

}

/****************************************************************************
 * Clear the complete CGA graphics screen.
 ****************************************************************************/
static void cga_clear (console_t *console) {

    uint32_t y;
    
    for (y = 0; y < height; y++) {
        clear_scanline (console, y);
    }
    
    console->cursor_x = 0;
    console->cursor_y = 0;

}

/****************************************************************************
 * Print one character.
 ****************************************************************************/
static void cga_putchar (console_t *console, char ch) {

    switch (ch) {
    
        case '\n':
        
            newline (console);
            break;
        
        case '\r':
        
            console->cursor_x = 0;
            break;
        
        case '\t':
        
            do {
                cga_putchar (console, ' ');
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
static void cga_erase_line (console_t *console, uint32_t row, uint32_t first, uint32_t last) {

    uint32_t column;
    
    for (column = first; column <= last; column++) {
        draw_char (console, column * FONT_WIDTH, row * FONT_HEIGHT, ' ');
    }

}

/****************************************************************************
 * Print a null-terminated string.
 ****************************************************************************/
/*static void cga_puts (console_t *console, const char *str) {

    while (*str != '\0') {
    
        cga_putchar (console, *str);
        str++;
    
    }

}*/

/******************************************************************************
 * Return width.
 *****************************************************************************/
static uint32_t cga_get_width (void) {
    return width / FONT_WIDTH;
}

/******************************************************************************
 * Return height.
 *****************************************************************************/
static uint32_t cga_get_height (void) {
    return height / FONT_HEIGHT;
}

/******************************************************************************
 * Set cursor position.
 *****************************************************************************/
static void cga_move_cursor (console_t *console, uint32_t x, uint32_t y) {

    console->cursor_x = x * FONT_WIDTH;
    console->cursor_y = y * FONT_HEIGHT;

}

static void cga_cursor (console_t *console, int show) {

    uint32_t row, col;
    size_t offset;
    
    if (!console->active || console->cursor_x + FONT_WIDTH > width || console->cursor_y + FONT_HEIGHT > console->usable_height) {
        return;
    }
    
    if (ttf_font_loaded) {
    
        if (!console->buffer) {
            return;
        }
        
        for (row = 0; row < CONSOLE_CURSOR_HEIGHT; row++) {
        
            uint32_t y = console->cursor_y + FONT_HEIGHT - CONSOLE_CURSOR_HEIGHT + row;
            
            for (col = 0; col < FONT_HEIGHT; col++) {
            
                uint32_t x = console->cursor_x + col;
                
                uint8_t shift = (uint8_t) ((3 - (x & 3)) << 1);
                uint8_t mask = (uint8_t) (3 << shift);
                
                uint8_t value;
                
                offset = scanline_offset (y) + (x >> 2);
                value = show ? (uint8_t) (console->foreground << shift) : console->buffer[offset];
                
                buffer[offset] = (uint8_t) ((buffer[offset] & (uint8_t) ~mask) | (value & mask));
            
            }
        
        }
        
        return;
    
    }
    
    for (row = 0; row < CONSOLE_CURSOR_HEIGHT; row++) {
    
        offset = scanline_offset (console->cursor_y + FONT_HEIGHT - CONSOLE_CURSOR_HEIGHT + row) + (console->cursor_x >> 2);
        
        if (show) {
        
            console->cursor_saved[row] = buffer[offset];
            buffer[offset] = (uint8_t) ((console->foreground & 3) * 0x55);
        
        } else {
            buffer[offset] = (uint8_t) console->cursor_saved[row];
        }
    
    }

}

/****************************************************************************
 * Refresh the screen so that the contents are updated.
 ****************************************************************************/
static void cga_refresh (console_t *console) {

    uint8_t *source, *destination;
    size_t i;
    
    if (console->active) {
    
        source = (uint8_t *) (uintptr_t) console->buffer;
        destination = (uint8_t *) (uintptr_t) buffer;
        
        for (i = 0; i < CGA_BUFFER_SIZE; ++i) {
            destination[i] = source[i];
        }
    
    }

}

/****************************************************************************
 * Initialize CGA graphics output.
 ****************************************************************************/
int cga_initialize (console_t *console, int need_buffer, const cga_info_t *info) {

    buffer = (volatile uint8_t *) (uintptr_t) info->address;
    
    width = info->width;
    height = info->height;
    
    console->usable_height = height - (height % FONT_HEIGHT);
    
    /* CGA mode 4 has four pixels per byte. */
    bytes_per_line = width / 4;
    
    console->cursor_x = 0;
    console->cursor_y = 0;
    
    console->foreground = 3;
    console->background = 0;
    
    if (need_buffer) {
    
        console->buffer_size = CGA_BUFFER_SIZE;
        
        if (!(console->buffer = kmalloc (console->buffer_size))) {
            return 1;
        }
    
    }
    
    cga_clear (console);
    cga_refresh (console);
    
    console->erase_line = cga_erase_line;
    console->cell_width = FONT_WIDTH;
    console->cell_height = FONT_HEIGHT;
    
    console->move_cursor = cga_move_cursor;
    console->cursor = cga_cursor;
    
    console->clear = cga_clear;
    console->putchar = cga_putchar;
    
    console->get_width = cga_get_width;
    console->get_height = cga_get_height;
    
    console->make_colour = cga_rgb_to_colour;
    console->refresh = cga_refresh;
    
    return 0;

}


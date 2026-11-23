/******************************************************************************
 * @file            textmode.c
 *****************************************************************************/
#include    <corazon/textmode.h>

#include    <stddef.h>
#include    <stdint.h>

static volatile uint16_t *buffer = 0;

static uint32_t width = 0;
static uint32_t height = 0;

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;

static uint8_t colour = 0;

/******************************************************************************
 * Write a byte to an I/O port.
 *****************************************************************************/
static void outb (uint16_t port, uint8_t value) {
    __asm__ volatile ( "outb %0, %1" : : "a" (value), "Nd" (port) );
}

/******************************************************************************
 * Build a VGA text-mode cell.
 *****************************************************************************/
static uint16_t make_entry (char ch, uint8_t attr) {
    return (uint16_t) (uint8_t) ch | ((uint16_t) attr << 8);
}

/******************************************************************************
 * Update the hardware VGA cursor.
 *****************************************************************************/
static void update_cursor (void) {

    uint16_t position = (uint16_t) ((cursor_y * width) + cursor_x);
    
    outb (0x3D4, 0x0F);
    outb (0x3D5, position & 0xFF);
    
    outb (0x3D4, 0x0E);
    outb (0x3D5, (position >> 8) & 0xFF);

}

/******************************************************************************
 * Clear one complete row.
 *****************************************************************************/
static void clear_row (uint32_t row) {

    uint32_t x;
    
    for (x = 0; x < width; x++) {
        buffer[(row * width) + x] = make_entry (' ', colour);
    }

}

/******************************************************************************
 * Scroll the screen upward by one text row.
 *****************************************************************************/
static void scroll (void) {

    uint32_t x;
    uint32_t y;
    
    for (y = 1; y < height; y++) {
    
        for (x = 0; x < width; x++) {
        
            buffer[((y - 1) * width) + x] =
            buffer[(y * width) + x];
        
        }
    
    }
    
    clear_row (height - 1);

}

/******************************************************************************
 * Move to the beginning of the next line.
 *****************************************************************************/
static void newline (void) {

    cursor_x = 0;
    cursor_y++;
    
    if (cursor_y >= height) {
    
        scroll ();
        cursor_y = height - 1;
    
    }
    
    update_cursor ();

}

/******************************************************************************
 * Convert an RGB colour to the nearest TEXTMODE colour.
 *****************************************************************************/
static uint8_t textmode_rgb_to_colour (uint8_t r, uint8_t g, uint8_t b) {

    static const uint8_t textmode_palette[16][3] = {
    
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
    
        int32_t dr = (int32_t) r - textmode_palette[i][0];
        int32_t dg = (int32_t) g - textmode_palette[i][1];
        int32_t db = (int32_t) b - textmode_palette[i][2];
        
        uint32_t distance = (uint32_t) ((dr * dr) + (dg * dg) + (db * db));
        
        if (distance < best_distance) {
        
            best_distance = distance;
            best_colour = i;
        
        }
    
    }
    
    return best_colour;

}

/******************************************************************************
 * Initialize VGA text-mode output.
 *****************************************************************************/
void textmode_initialize (const textmode_info_t *info) {

    buffer = (volatile uint16_t *) info->address;
    
    width = info->width;
    height = info->height;
    
    cursor_x = 0;
    cursor_y = 0;
    
    /* Light grey foreground on black background. */
    colour = 0x07;
    update_cursor ();

}

/******************************************************************************
 * Clear the complete text-mode screen.
 *****************************************************************************/
void textmode_clear (void) {

    uint32_t y;
    
    for (y = 0; y < height; y++) {
        clear_row (y);
    }
    
    cursor_x = 0;
    cursor_y = 0;
    
    update_cursor ();

}

/******************************************************************************
 * Print one character.
 *****************************************************************************/
void textmode_putchar (char ch) {

    uint32_t tab_stop;
    
    switch (ch) {
    
        case '\n':
        
            newline ();
            return;
        
        case '\r':
        
            cursor_x = 0;
            
            update_cursor ();
            return;
        
        case '\b':
        
            if (cursor_x > 0) {
                cursor_x--;
            } else if (cursor_y > 0) {
            
                cursor_y--;
                cursor_x = width - 1;
            
            } else {
                return;
            }
            
            buffer[(cursor_y * width) + cursor_x] = make_entry (' ', colour);
            
            update_cursor ();
            return;
        
        case '\t':
        
            tab_stop = ((cursor_x / 4) + 1) * 4;
            
            while (cursor_x < tab_stop) {
            
                textmode_putchar (' ');
                
                if (cursor_x == 0) {
                    break;
                }
            
            }
            
            return;
        
        default:
        
            break;
    
    }
    
    buffer[(cursor_y * width) + cursor_x] = make_entry (ch, colour);
    cursor_x++;
    
    if (cursor_x >= width) {
        newline ();
    } else {
        update_cursor ();
    }

}


/******************************************************************************
 * Print a null-terminated string.
 *****************************************************************************/
void textmode_puts (const char *str) {

    while (*str != '\0') {

        textmode_putchar (*str);
        str++;
    
    }

}

/******************************************************************************
 * Set the foreground and background colours.
 *****************************************************************************/
void textmode_set_colour (uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg, uint8_t bb) {

    uint8_t foreground;
    uint8_t background;
    
    foreground = textmode_rgb_to_colour (fr, fg, fb);
    background = textmode_rgb_to_colour (br, bg, bb);
    
    colour = (uint8_t) ((background << 4) | foreground);

}

/******************************************************************************
 * Return width.
 *****************************************************************************/
uint32_t textmode_get_width (void) {
    return width;
}

/******************************************************************************
 * Return height.
 *****************************************************************************/
uint32_t textmode_get_height (void) {
    return height;
}

/******************************************************************************
 * Set cursor position.
 *****************************************************************************/
void textmode_move_cursor (uint32_t x, uint32_t y) {

    cursor_x = x;
    cursor_y = y;

}

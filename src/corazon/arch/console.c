/******************************************************************************
 * @file            console.c
 *****************************************************************************/
#include    <corazon/tasking/process.h>
#include    <corazon/utils/misc.h>
#include    <corazon/video/cga.h>
#include    <corazon/video/ega.h>
#include    <corazon/video/framebuffer.h>
#include    <corazon/video/ttf.h>
#include    <corazon/console.h>

#include    <errno.h>
#include    <stdint.h>
#include    <string.h>

static console_t idle_console = { 0 };

static console_t *g_console = 0;
static console_info_t console_info = { 0 };

static uintptr_t console_save_interrupts (void) {
    
    uintptr_t flags;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("pushfq");
#else
    __asm__ __volatile__ ("pushfd");
#endif
    
    __asm__ __volatile__ ("pop %0" : "=r" (flags) : : "memory");
    __asm__ __volatile__ ("cli");
    
    return flags;

}

static void console_restore_interrupts (uintptr_t flags) {
    
    __asm__ __volatile__ ("push %0" : : "r" (flags) : "memory");

#if     defined (__AMD64__)
    __asm__ __volatile__ ("popfq");
#else
    __asm__ __volatile__ ("popfd");
#endif

}

static void console_hide_cursor (console_t *console) {

    if (console && console->cursor_visible && console->cursor) {
    
        console->cursor (console, 0);
        console->cursor_visible = 0;
    
    }

}

static void console_show_cursor (console_t *console) {

    if (console && console->active && console->cursor_enabled && !console->cursor_visible && console->cursor) {
    
        console->cursor (console, 1);
        console->cursor_visible = 1;
    
    }

}

/******************************************************************************
 * Create a new console.
 *****************************************************************************/
struct console *console_create (void) {

    console_t *console;
    int ret = 0;
    
    if (!(console = kmalloc (sizeof (*console)))) {
        return 0;
    }
    
    memset (console, 0, sizeof (*console));
    console->termios.c_lflag = ICANON | ECHO;
    
    console->termios.c_cc[VEOF] = 4;
    console->termios.c_cc[VMIN] = 1;
    
    console->cursor_enabled = 1;
    
    switch (console_info.type) {
    
        /*
        case CONSOLE_TYPE_TEXTMODE:
        
            textmode_initialize (console, &console_info.textmode);
            break;
        */
        case CONSOLE_TYPE_CGA:
        
            ret = cga_initialize (console, 1, &console_info.cga);
            break;
        
        case CONSOLE_TYPE_EGA:
        
            ret = ega_initialize (console, 1, &console_info.ega);
            break;
        
        case CONSOLE_TYPE_FRAMEBUFFER:
        
            ret = framebuffer_initialize (console, 1, &console_info.framebuffer);
            break;
        
        default:
        
            break;
    
    }
    
    if (ret) {
    
        kfree (console);
        return 0;
    
    }
    
    console->default_foreground = console->foreground;
    console->default_background = console->background;
    
    console->ansi_foreground = 255;
    return console;

}

/******************************************************************************
 * Get the current active console.
 *****************************************************************************/
struct console *console_get_current (void) {
    return g_console;
}

/******************************************************************************
 * Get the current console type.
 *****************************************************************************/
uint32_t console_get_type (void) {
    return console_info.type;
}

/******************************************************************************
 * Free memory related to a specific console.
 *****************************************************************************/
void console_destroy (console_t *console) {

    if (console) {
    
        if (console->buffer) {
            kfree (console->buffer);
        }
        
        if (console->screen_buffer) {
            kfree (console->screen_buffer);
        }
        
        if (console->input) {
            kfree (console->input);
        }
        
        kfree (console);
    
    }

}

/******************************************************************************
 * Initialize the appropriate console backend.
 *****************************************************************************/
void console_initialize (const console_info_t *info) {

    memcpy (&console_info, info, sizeof (*info));
    
    switch (console_info.type) {
    
        /*
        case CONSOLE_TYPE_TEXTMODE:
        
            textmode_initialize (console, &console_info.textmode);
            break;
        */
        case CONSOLE_TYPE_CGA:
        
            cga_initialize (&idle_console, 0, &console_info.cga);
            break;
        
        case CONSOLE_TYPE_EGA:
        
            ega_initialize (&idle_console, 0, &console_info.ega);
            break;
        
        case CONSOLE_TYPE_FRAMEBUFFER:
        
            framebuffer_initialize (&idle_console, 0, &console_info.framebuffer);
            break;
        
        default:
        
            break;
    
    }
    
    g_console = &idle_console;
    
    g_console->default_foreground = g_console->foreground;
    g_console->default_background = g_console->background;
    
    g_console->ansi_foreground = 255;
    g_console->cursor_enabled = 1;
    g_console->active = 1;
    
    g_console->termios.c_lflag = ICANON | ECHO;
    
    g_console->termios.c_cc[VEOF] = 4;
    g_console->termios.c_cc[VMIN] = 1;
    
    console_show_cursor (g_console);

}

/******************************************************************************
 * Set the active console.
 *****************************************************************************/
void console_switch (console_t *console) {

    uintptr_t flags = console_save_interrupts ();
    
    if (g_console) {
    
        console_hide_cursor (g_console);
        g_console->active = 0;
    
    }
    
    if ((g_console = console)) {
    
        g_console->active = 1;
        
        if (ttf_font_loaded && (g_console->cell_width != ttf_font_width || g_console->cell_height != ttf_font_height)) {
        
            g_console->cell_width = ttf_font_width;
            g_console->cell_height = ttf_font_height;
            
            g_console->usable_height = console_get_pixel_height () - (console_get_pixel_height () % ttf_font_height);
            
            if (g_console->buffer && g_console->clear) {
                g_console->clear (g_console);
            }
        }
        
        if (g_console->refresh) {
            g_console->refresh (g_console);
        }
        
        console_show_cursor (g_console);
    
    }
    
    console_restore_interrupts (flags);

}

/******************************************************************************
 * Return the full video mode height in pixels.
 *****************************************************************************/
uint32_t console_get_pixel_height (void) {

    switch (console_info.type) {
    
        case CONSOLE_TYPE_CGA:
        
            return console_info.cga.height;
        
        case CONSOLE_TYPE_EGA:
        
            return console_info.ega.height;
        
        case CONSOLE_TYPE_FRAMEBUFFER:
        
            return console_info.framebuffer.height;
        
        default:
        
            return 0;
    
        }

}

/******************************************************************************
 * Return the full video mode width in pixels.
 *****************************************************************************/
uint32_t console_get_pixel_width (void) {

    switch (console_info.type) {
    
        case CONSOLE_TYPE_CGA:
        
            return console_info.cga.width;
        
        case CONSOLE_TYPE_EGA:
        
            return console_info.ega.width;
        
        case CONSOLE_TYPE_FRAMEBUFFER:
        
            return console_info.framebuffer.width;
        
        default:
        
            return 0;
    
        }

}

/******************************************************************************
 * Activate a loaded font after hiding the old cursor.
 *****************************************************************************/
void console_font_activate (void) {

    uintptr_t flags = console_save_interrupts ();
    
    console_hide_cursor (g_console);
    ttf_font_loaded = 1;
    
    if (g_console) {
    
        g_console->cell_width = ttf_font_width;
        g_console->cell_height = ttf_font_height;
        
        g_console->usable_height = console_get_pixel_height () - (console_get_pixel_height () % ttf_font_height);
        
        if (g_console->buffer && g_console->clear) {
            g_console->clear (g_console);
        }
        
        if (g_console->buffer && g_console->refresh) {
            g_console->refresh (g_console);
        }
    
    }
    
    console_show_cursor (g_console);
    console_restore_interrupts (flags);

}

/******************************************************************************
 * Clear the console.
 *****************************************************************************/
void console_clear (void) {

    struct thread *thread = 0;
    console_t *console = 0;
    
    uintptr_t flags = console_save_interrupts ();
    
    if ((thread = thread_get_current ()) && thread->owner) {
        console = thread->owner->console;
    }
    
    if (!console) { console = g_console; }
    
    if (!console || !console->clear) {
    
        console_restore_interrupts (flags);
        return;
    
    }
    
    console_hide_cursor (console);
    console->clear (console);
    
    if (console->active && console->refresh) {
        console->refresh (console);
    }
    
    console_show_cursor (console);
    console_restore_interrupts (flags);

}

/******************************************************************************
 * Return the video mode width.
 *****************************************************************************/
uint32_t console_get_width (void) {

    struct thread *thread = 0;
    console_t *console = 0;
    
    if ((thread = thread_get_current ()) && thread->owner) {
        console = thread->owner->console;
    }
    
    if (!console) { console = g_console; }
    
    if (!console || !console->get_width) {
        return 0;
    }
    
    return console->get_width ();

}

/******************************************************************************
 * Return the video mode height.
 *****************************************************************************/
uint32_t console_get_height (void) {

    struct thread *thread = 0;
    console_t *console = 0;
    
    if ((thread = thread_get_current ()) && thread->owner) {
        console = thread->owner->console;
    }
    
    if (!console) { console = g_console; }
    
    if (!console || !console->get_height) {
        return 0;
    }
    
    return console->get_height ();

}

/******************************************************************************
 * Set cursor position.
 *****************************************************************************/
void console_move_cursor (uint32_t x, uint32_t y) {

    struct thread *thread = 0;
    console_t *console = 0;
    
    uintptr_t flags = console_save_interrupts ();
    
    if ((thread = thread_get_current ()) && thread->owner) {
        console = thread->owner->console;
    }
    
    if (!console) { console = g_console; }
    
    if (!console || !console->move_cursor) {
    
        console_restore_interrupts (flags);
        return;
    
    }
    
    console_hide_cursor (console);
    console->move_cursor (console, x, y);
    
    console_show_cursor (console);
    console_restore_interrupts (flags);

}

/* Apply reverse video to subsequent characters. */
static void console_reverse_video (console_t *console, int enabled) {

    uint32_t colour;
    
    if (console->reverse_video == enabled) {
        return;
    }
    
    colour = console->foreground;
    
    console->foreground = console->background;
    console->background = colour;
    console->reverse_video = enabled;

}

/* Convert an ANSI palette index to the active console colour. */
static uint32_t console_ansi_colour (console_t *console, uint32_t index) {

    static const uint8_t palette[16][3] = {
    
        {   0,   0,   0 }, { 170,   0,   0 }, {   0, 170,   0 }, { 170,  85,   0 },
        {   0,   0, 170 }, { 170,   0, 170 }, {   0, 170, 170 }, { 170, 170, 170 },
        {  85,  85,  85 }, { 255,  85,  85 }, {  85, 255,  85 }, { 255, 255,  85 },
        {  85,  85, 255 }, { 255,  85, 255 }, {  85, 255, 255 }, { 255, 255, 255 }
    
    };
    
    static const uint8_t cube[6] = { 0, 95, 135, 175, 215, 255 };
    uint8_t r, g, b;
    
    if (index < 16) {
    
        r = palette[index][0];
        g = palette[index][1];
        b = palette[index][2];
    
    } else if (index < 232) {
    
        index -= 16;
        
        r = cube[index / 36];
        g = cube[(index / 6) % 6];
        b = cube[index % 6];
    
    } else {
        r = g = b = (uint8_t) (8 + (index - 232) * 10);
    }
    
    return console->make_colour (r, g, b);

}

/* Set a logical ANSI colour, including while reverse video is active. */
static void console_ansi_set_colour (console_t *console, int foreground, uint32_t colour) {

    if ((foreground && !console->reverse_video) || (!foreground && console->reverse_video)) {
        console->foreground = colour;
    } else {
        console->background = colour;
    }

}

/* Swap between the primary and alternate screen buffers. */
static void console_alternate_screen (console_t *console, int enable) {

    uint8_t *buffer;
    
    if (!console->buffer || !console->buffer_size || console->alternate_screen == enable) {
        return;
    }
    
    if (enable) {
    
        if (!console->screen_buffer && !(console->screen_buffer = kmalloc (console->buffer_size))) {
            return;
        }
        
        console->saved_cursor_x = console->cursor_x;
        console->saved_cursor_y = console->cursor_y;
        console->saved_foreground = console->foreground;
        console->saved_background = console->background;
        console->saved_ansi_foreground = console->ansi_foreground;
        console->saved_ansi_bold = console->ansi_bold;
        console->saved_reverse_video = console->reverse_video;
        console->saved_cursor_enabled = console->cursor_enabled;
    
    }
    
    buffer = console->buffer;
    
    console->buffer = console->screen_buffer;
    console->screen_buffer = buffer;
    console->alternate_screen = enable;
    
    if (enable) {
        console->clear (console);
    } else {
    
        console->cursor_x = console->saved_cursor_x;
        console->cursor_y = console->saved_cursor_y;
        console->foreground = console->saved_foreground;
        console->background = console->saved_background;
        console->ansi_foreground = console->saved_ansi_foreground;
        console->ansi_bold = console->saved_ansi_bold;
        console->reverse_video = console->saved_reverse_video;
        console->cursor_enabled = console->saved_cursor_enabled;
    
    }
    
    if (console->active && console->refresh) {
        console->refresh (console);
    }

}

/* Execute a completed ANSI control sequence. */
static void console_control_sequence (console_t *console, char command) {

    uint32_t x, y, columns, rows, value, mode;
    uint32_t i;
    
    if (console->escape_private) {
    
        if (console->escape_parameters[0] == 1049) {
        
            if (command == 'h' || command == 'l') {
                console_alternate_screen (console, command == 'h');
            }
        
        } else if (console->escape_parameters[0] == 25) {
        
            if (command == 'h') {
                console->cursor_enabled = 1;
            } else if (command == 'l') {
                console->cursor_enabled = 0;
            }
        
        }
        
        return;
    
    }
    
    if (command == 'm') {
    
        if (!console->make_colour) {
            return;
        }
        
        for (i = 0; i <= console->escape_count; i++) {
        
            value = console->escape_parameters[i];
            
            if (value == 0) {
            
                console_reverse_video (console, 0);
                
                console->foreground = console->default_foreground;
                console->background = console->default_background;
                
                console->ansi_bold = 0;
                console->ansi_foreground = 255;
            
            } else if (value == 1 || value == 22) {
            
                console->ansi_bold = value == 1;
                
                if (console->ansi_foreground < 8) {
                    console_ansi_set_colour (console, 1, console_ansi_colour (console, console->ansi_foreground + (console->ansi_bold ? 8 : 0)));
                }
            } else if (value == 7) {
                console_reverse_video (console, 1);
            } else if (value == 27) {
                console_reverse_video (console, 0);
            } else if (value >= 30 && value <= 37) {
            
                console->ansi_foreground = (uint8_t) (value - 30);
                console_ansi_set_colour (console, 1, console_ansi_colour (console, console->ansi_foreground + (console->ansi_bold ? 8 : 0)));
            
            } else if (value >= 40 && value <= 47) {
                console_ansi_set_colour (console, 0, console_ansi_colour (console, value - 40));
            } else if (value >= 90 && value <= 97) {
            
                console->ansi_foreground = 255;
                console_ansi_set_colour (console, 1, console_ansi_colour (console, value - 90 + 8));
            
            } else if (value >= 100 && value <= 107) {
                console_ansi_set_colour (console, 0, console_ansi_colour (console, value - 100 + 8));
            } else if (value == 39) {
            
                console->ansi_foreground = 255;
                console_ansi_set_colour (console, 1, console->default_foreground);
            
            } else if (value == 49) {
                console_ansi_set_colour (console, 0, console->default_background);
            } else if (value == 38 || value == 48) {
            
                uint32_t colour;
                
                if (i + 2 <= console->escape_count && console->escape_parameters[i + 1] == 5 && console->escape_parameters[i + 2] < 256) {
                
                    colour = console_ansi_colour (console, console->escape_parameters[i + 2]);
                    i += 2;
                
                } else if (i + 4 <= console->escape_count && console->escape_parameters[i + 1] == 2 &&
                    console->escape_parameters[i + 2] < 256 && console->escape_parameters[i + 3] < 256 && console->escape_parameters[i + 4] < 256) {
                
                    colour = console->make_colour ((uint8_t) console->escape_parameters[i + 2],
                        (uint8_t) console->escape_parameters[i + 3], (uint8_t) console->escape_parameters[i + 4]);
                    i += 4;
                
                } else {
                    continue;
                }
                
                if (value == 38) {
                    console->ansi_foreground = 255;
                }
                
                console_ansi_set_colour (console, value == 38, colour);
            
            }
        
        }
        
        return;
    
    }
    
    if (!console->move_cursor || !console->get_width || !console->get_height || !console->cell_width || !console->cell_height) {
        return;
    }
    
    columns = console->get_width ();
    rows = console->get_height ();
    
    if (!columns || !rows) {
        return;
    }
    
    x = console->cursor_x / console->cell_width;
    y = console->cursor_y / console->cell_height;
    
    if (x >= columns) { x = columns - 1; }
    if (y >= rows) { y = rows - 1; }
    
    value = console->escape_parameters[0] ? console->escape_parameters[0] : 1;
    
    switch (command) {
    
        case 'A':
        
            y = value > y ? 0 : y - value;
            break;
        
        case 'B':
        
            y = value >= rows - y ? rows - 1 : y + value;
            break;
        
        case 'C':
        
            x = value >= columns - x ? columns - 1 : x + value;
            break;
        
        case 'D':
        
            x = value > x ? 0 : x - value;
            break;
        
        case 'G':
        
            x = value > columns ? columns - 1 : value - 1;
            break;
        
        case 'H':   case 'f':
        
            y = value > rows ? rows - 1 : value - 1;
            
            value = console->escape_count && console->escape_parameters[1] ? console->escape_parameters[1] : 1;
            x = value > columns ? columns - 1 : value - 1;
            
            break;
        
        case 'K':
        
            if (!console->erase_line) {
                return;
            }
            
            mode = console->escape_parameters[0];
            
            if (mode == 0) {
                console->erase_line (console, y, x, columns - 1);
            } else if (mode == 1) {
                console->erase_line (console, y, 0, x);
            } else if (mode == 2) {
                console->erase_line (console, y, 0, columns - 1);
            }
            
            return;
        
        case 'J':
        
            if (!console->erase_line) {
                return;
            }
            
            mode = console->escape_parameters[0];
            
            if (mode > 2) {
                return;
            }
            
            for (i = 0; i < rows; i++) {
            
                if (mode == 0 && i < y) { continue; }
                if (mode == 1 && i > y) { continue; }
                
                console->erase_line (console, i,
                    mode == 0 && i == y ? x : 0,
                        mode == 1 && i == y ? x : columns - 1);
            
            }
            
            return;
        
        default:
        
            return;
    
    }
    
    console->move_cursor (console, x, y);

}

/* Interpret output sequences across successive writes. */
static void console_write_character (console_t *console, char ch) {

    uint32_t *parameter;
    
    if (console->escape_state == 0) {
    
        if (ch == '\x1b') {
            console->escape_state = 1;
        } else if ((int) ch >= ' ' || ch == '\n' || ch == '\r' || ch == '\t' || ch == '\b') {
            console->putchar (console, ch);
        }
        
        return;
    
    }
    
    if (console->escape_state == 1) {
    
        console->escape_state = 0;
        
        if (ch == '[') {
        
            console->escape_state = 2;
            console->escape_private = 0;
            console->escape_count = 0;
            console->escape_parameters[0] = 0;
        
        } else if (ch == '\x1b') {
            console->escape_state = 1;
        }
        
        return;
    
    }
    
    if (ch == '?' && !console->escape_private && !console->escape_count && !console->escape_parameters[0]) {
    
        console->escape_private = 1;
        return;
    
    }
    
    if (ch >= '0' && ch <= '9') {
    
        parameter = &console->escape_parameters[console->escape_count];
        
        if (*parameter > (UINT32_MAX - (ch - '0')) / 10) {
        
            console->escape_state = 0;
            return;
        
        }
        
        *parameter = *parameter * 10 + (ch - '0');
        return;
    
    }
    
    if (ch == ';') {
    
        if (console->escape_count >= 15) {
        
            console->escape_state = 0;
            return;
        
        }
        
        console->escape_parameters[++console->escape_count] = 0;
        return;
    
    }
    
    console->escape_state = 0;
    
    if (ch >= '@' && ch <= '~') {
        console_control_sequence (console, ch);
    }

}

/******************************************************************************
 * Print one character.
 *****************************************************************************/
void console_put_char (char ch) {

    struct thread *thread = 0;
    console_t *console = 0;
    
    uintptr_t flags = console_save_interrupts ();
    
    if ((thread = thread_get_current ()) && thread->owner) {
        console = thread->owner->console;
    }
    
    if (!console) { console = g_console; }
    
    if (!console || !console->putchar) {
    
        console_restore_interrupts (flags);
        return;
    
    }
    
    console_hide_cursor (console);
    console_write_character (console, ch);
    
    console_show_cursor (console);
    console_restore_interrupts (flags);

}

/******************************************************************************
 * Print a null-terminated string.
 *****************************************************************************/
void console_puts (const char *str) {

    /**
     * Deliberately go through console_putchar() rather than calling the
     * backend puts() function directly.
     *
     * This keeps character handling going through one common console path.
     */
    while (*str != '\0') {
    
        console_put_char (*str);
        str++;
    
    }

}

/******************************************************************************
 * Set the background colour.
 *****************************************************************************/
void console_setbackgound (uint8_t r, uint8_t g, uint8_t b) {

    struct thread *thread = 0;
    console_t *console = 0;
    
    if ((thread = thread_get_current ()) && thread->owner) {
        console = thread->owner->console;
    }
    
    if (!console) { console = g_console; }
    
    if (!console || !console->make_colour) {
        return;
    }
    
    console->default_background = console->make_colour (r, g, b);
    console_ansi_set_colour (console, 0, console->default_background);

}

/******************************************************************************
 * Set the foreground colour.
 *****************************************************************************/
void console_setforegound (uint8_t r, uint8_t g, uint8_t b) {

    struct thread *thread = 0;
    console_t *console = 0;
    
    if ((thread = thread_get_current ()) && thread->owner) {
        console = thread->owner->console;
    }
    
    if (!console) { console = g_console; }
    
    if (!console || !console->make_colour) {
        return;
    }
    
    console->default_foreground = console->make_colour (r, g, b);
    console->ansi_foreground = 255;
    
    console_ansi_set_colour (console, 1, console->default_foreground);

}


#define     CONSOLE_INPUT_SIZE          256

/* Grow the input buffer by CONSOLE_INPUT_SIZE while preserving queued input. */
static int console_grow_input (console_t *console) {

    char *input;
    uint32_t capacity, i;
    
    if (!CONSOLE_INPUT_SIZE || console->input_capacity > UINT32_MAX - CONSOLE_INPUT_SIZE) {
        return 0;
    }
    
    capacity = console->input_capacity + CONSOLE_INPUT_SIZE;
    
    if (!(input = kmalloc (capacity))) {
        return 0;
    }
    
    for (i = 0; i < console->input_count; i++) {
        input[i] = console->input[(console->input_head + i) % console->input_capacity];
    }
    
    if (console->input) {
        kfree (console->input);
    }
    
    console->input = input;
    console->input_head = 0;
    
    console->input_capacity = capacity;
    return 1;

}

/* Copy one completed line without consuming it on a failed user copy. */
static int console_copy_input (console_t *console, struct thread *thread, uintptr_t address, size_t count) {

    size_t i, length, first, ready, consumed;
    
    ready = console->input_ready;
    length = (console->termios.c_lflag & ICANON) ? ready : console->input_count;
    
    if (length > count) {
        length = count;
    }
    
    consumed = length;
    
    for (i = 0; i <= length && i < ready; i++) {
    
        char ch = console->input[(console->input_head + i) % console->input_capacity];
        
        if (ch == '\0') {
        
            length = i;
            consumed = i + 1;
            
            break;
        
        }
        
        if (i < length && ch == '\n' && (console->termios.c_lflag & ICANON)) {
        
            length = i + 1;
            consumed = length;
            
            break;
        
        }
    
    }
    
    first = console->input_capacity - console->input_head;
    
    if (first > length) {
        first = length;
    }
    
    if (length && thread->user_mode) {
    
        if (!mmu_copy_user (thread->owner->pd, address, console->input + console->input_head, first, 1) ||
            (length > first && !mmu_copy_user (thread->owner->pd, address + first, console->input, length - first, 1))) {
            return -EFAULT;
        }
    
    } else if (length) {
    
        memcpy ((void *) address, console->input + console->input_head, first);
        
        if (length > first) {
            memcpy ((void *) (address + first), console->input, length - first);
        }
    
    }
    
    console->input_head = (console->input_head + consumed) % console->input_capacity;
    console->input_count -= consumed;
    
    if (consumed > ready) {
    
        console->input_ready = 0;
        console->input_uncommitted -= consumed - ready;
    
    } else {
        console->input_ready -= consumed;
    }
    
    if (console->input_echoed >= consumed) {
        console->input_echoed -= consumed;
    } else {
        console->input_echoed = 0;
    }
    
    return (int) length;

}

/* Echo buffered typeahead once a console read has been requested. */
static void console_echo_input (console_t *console) {

    if (!(console->termios.c_lflag & ECHO) || !console->putchar || console->input_echoed == console->input_count) {
        return;
    }
    
    console_hide_cursor (console);
    
    while (console->input_echoed < console->input_count) {
    
        char ch = console->input[(console->input_head + console->input_echoed) % console->input_capacity];
        
        if (ch) {
            console->putchar (console, ch);
        }
        
        console->input_echoed++;
    
    }
    
    console_show_cursor (console);

}

/* Feed the active console from the keyboard interrupt handler. */
void console_input_char (char ch) {

    console_t *console;
    
    struct thread *reader;
    uintptr_t flags;
    
    int result;
    
    flags = console_save_interrupts ();
    console = g_console;
    
    if (!console) {
    
        console_restore_interrupts (flags);
        return;
    
    }
    
    console_hide_cursor (console);
    
    if (ch == '\x03' && (console->termios.c_lflag & ISIG)) {
    
        console->input_head = 0;
        console->input_count = 0;
        console->input_ready = 0;
        console->input_echoed = 0;
        console->input_uncommitted = 0;
        
        if (console->putchar) {
        
            console->putchar (console, '^');
            console->putchar (console, 'C');
            console->putchar (console, '\n');
        
        }
        
        process_interrupt_console (console);
        
        console_show_cursor (console);
        console_restore_interrupts (flags);
        
        return;
    
    }
    
    if ((console->termios.c_lflag & ICANON) && console->termios.c_cc[VEOF] && (unsigned char) ch == console->termios.c_cc[VEOF]) {
    
        if (console->input_count < console->input_capacity || console_grow_input (console)) {
        
            /* NUL is reserved for queued EOF boundaries; keyboard input never supplies it. */
            console->input[(console->input_head + console->input_count) % console->input_capacity] = '\0';
            console->input_count++;
            
            console->input_ready = console->input_count;
            console->input_uncommitted = 0;
        
        }
    
    } else if ((ch == '\b' || ch == 127) && (console->termios.c_lflag & ICANON)) {
    
        if (console->input_uncommitted) {
        
            console->input_count--;
            console->input_uncommitted--;
            
            if (console->input_echoed > console->input_count) {
            
                console->input_echoed--;
                
                if ((console->termios.c_lflag & ECHO) && console->input_reader && console->putchar) {
                    console->putchar (console, '\b');
                }
            
            }
        
        }
    
    } else if (!(console->termios.c_lflag & ICANON) || ch == '\n' || ((unsigned char) ch >= ' ' && (unsigned char) ch != 0x7F) || ch == '\t') {
    
        if (console->input_count < console->input_capacity || console_grow_input (console)) {
        
            console->input[(console->input_head + console->input_count) % console->input_capacity] = ch;
            console->input_count++;
            
            if (ch == '\n') {
            
                console->input_ready = console->input_count;
                console->input_uncommitted = 0;
            
            } else {
                console->input_uncommitted++;
            }
            
            if (!(console->termios.c_lflag & ECHO)) {
                console->input_echoed = console->input_count;
            } else if (console->input_reader && console->putchar) {
            
                console->putchar (console, ch);
                console->input_echoed++;
            
            }
        
        }
    
    }
    
    console_show_cursor (console);
    reader = console->input_reader;
    
    if (reader && ((console->termios.c_lflag & ICANON) ? console->input_ready : console->input_count)) {
    
        result = console_copy_input (console, reader, console->input_address, console->input_length);
        
#if     defined (__AMD64__)
        reader->regs.rax = (uintptr_t) result;
#else
        reader->regs.eax = (uintptr_t) result;
#endif
        
        reader->state = TS_RUN;
        reader->state_private_data = 0;
        
        console->input_reader = 0;
    
    }
    
    console_restore_interrupts (flags);

}

#include    <corazon/vfs/node.h>
#include    <corazon/vfs/vfs.h>

#include    <sys/ioctl.h>

static int console_read (void *data, void *buf, size_t count) {

    struct fs_node *node;
    console_t *console;
    
    struct thread *thread;
    uintptr_t flags;
    
    int result;
    
    if (!count) {
        return 0;
    }
    
    if (!(node = (struct fs_node *) data)) {
        return -EFAULT;
    }
    
    if (!(console = (console_t *) node->data)) {
        return -EFAULT;
    }
    
    if (!(thread = thread_get_current ())) {
        return -EFAULT;
    }
    
    if (!buf) {
        return -EFAULT;
    }
    
    if (thread->user_mode && !mmu_user_buffer_valid (thread->owner->pd, (uintptr_t) buf, count, 1)) {
        return -EFAULT;
    }
    
    flags = console_save_interrupts ();
    
    if (!console->input_reader) {
        console_echo_input (console);
    }
    
    if ((console->termios.c_lflag & ICANON) ? console->input_ready : console->input_count) {
    
        result = console_copy_input (console, thread, (uintptr_t) buf, count);
        
        console_restore_interrupts (flags);
        return result;
    
    }
    
    if (!thread->user_mode) {
    
        console_restore_interrupts (flags);
        return -EAGAIN;
    
    }
    
    if (console->input_reader) {
    
        console_restore_interrupts (flags);
        return -EBUSY;
    
    }
    
    console->input_reader = thread;
    console->input_address = (uintptr_t) buf;
    console->input_length = count;
    
    thread->state = TS_WAITIO;
    thread->state_private_data = console;
    
    console_restore_interrupts (flags);
    return -EAGAIN;

}

static int console_poll (void *data) {
    
    struct fs_node *node = (struct fs_node *) data;
    console_t *console;
    
    if (!node || !(console = (console_t *) node->data)) {
        return 0;
    }
    
    return VFS_POLLOUT | (((console->termios.c_lflag & ICANON) ? console->input_ready : console->input_count) ? VFS_POLLIN : 0);

}

static int console_ioctl (void *data, unsigned long request, void *arg) {

    struct fs_node *node = (struct fs_node *) data;
    struct thread *thread = thread_get_current ();
    
    struct termios attrs;
    
    console_t *console;
    uintptr_t flags;
    
    if (!node || !(console = (console_t *) node->data) || !thread || !arg) {
        return -EFAULT;
    }
    
    if (request == TIOCGWINSZ) {
    
        struct winsize size = { 0 };
        
        if (!console->get_width || !console->get_height) {
            return -ENOTTY;
        }
        
        flags = console_save_interrupts ();
        
        size.ws_col = (unsigned short) console->get_width ();
        size.ws_row = (unsigned short) console->get_height ();
        
        size.ws_xpixel = (unsigned short) (size.ws_col * console->cell_width);
        size.ws_ypixel = (unsigned short) (size.ws_row * console->cell_height);
        
        console_restore_interrupts (flags);
        
        if (thread->user_mode) {
            return mmu_copy_user (thread->owner->pd, (uintptr_t) arg, &size, sizeof (size), 1) ? 0 : -EFAULT;
        }
        
        memcpy (arg, &size, sizeof (size));
        return 0;
    
    }
    
    if (request == TCGETS) {
    
        flags = console_save_interrupts ();
        attrs = console->termios;
        
        console_restore_interrupts (flags);
        
        if (thread->user_mode) {
            return mmu_copy_user (thread->owner->pd, (uintptr_t) arg, &attrs, sizeof (attrs), 1) ? 0 : -EFAULT;
        }
        
        memcpy (arg, &attrs, sizeof (attrs));
        return 0;
    
    }
    
    if (request == TCSETS) {
    
        if (thread->user_mode) {
        
            if (!mmu_copy_user (thread->owner->pd, (uintptr_t) arg, &attrs, sizeof (attrs), 0)) {
                return -EFAULT;
            }
        
        } else {
            memcpy (&attrs, arg, sizeof (attrs));
        }
        
        if (!(attrs.c_lflag & ICANON) && (attrs.c_cc[VMIN] != 1 || attrs.c_cc[VTIME] != 0)) {
            return -EINVAL;
        }
        
        flags = console_save_interrupts ();
        
        console->termios = attrs;
        console_restore_interrupts (flags);
        
        return 0;
    
    }
    
    return -ENOTTY;

}

static int console_write (void *data, void *buf, size_t count) {

    uint8_t *tmp = (uint8_t *) buf;
    
    int written = 0;
    size_t i;
    
    for (i = 0; i < count; i++) {
    
        console_put_char (tmp[i]);
        written++;
    
    }
    
    (void) data;
    return written;

}

struct fs_node *console_open (void *data, const char *path, size_t depth) {

    struct fs_node *node = 0;
    struct thread *thread = 0;
    
    (void) data;
    (void) path;
    (void) depth;
    
    if (!(node = kmalloc (sizeof (*node)))) {
        return 0;
    }
    
    memset (node, 0, sizeof (*node));
    
    if ((thread = thread_get_current ()) && thread->owner) {
        node->data = thread->owner->console;
    }
    
    if (!node->data) {
        node->data = g_console;
    }
    
    node->flags  = INO_CHR;
    node->kioctl = console_ioctl;
    node->kpoll  = console_poll;
    node->kread  = console_read;
    node->kwrite = console_write;
    
    return node;

}

void console_install (void) {

    const char *name = "/dev/console";
    struct fs_node *node = 0;
    
    if (node = kmalloc (sizeof (*node))) {
    
        memset (node, 0, sizeof (*node));
        
        node->kopen = console_open;
        node->flags = INO_CHR;
        
        vfs_mount (name, node);
    
    }

}

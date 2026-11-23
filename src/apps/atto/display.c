/******************************************************************************
 * @file            display.c
 *****************************************************************************/
#include    <stdio.h>
#include    <string.h>
#include    <time.h>
#include    <unistd.h>

#include    "buffer.h"
#include    "display.h"
#include    "editor.h"

static size_t calculate_visible_line_length (size_t filerow) {

    size_t len = g_editor.rows[filerow].size;
    
    if (len > g_editor.coloff) {
        len -= g_editor.coloff;
    } else {
        len = 0;
    }
    
    if (len > g_editor.screen_cols) {
        len = g_editor.screen_cols;
    }
    
    return len;

}

void display_draw_file_row (append_buffer_t *ab, size_t filerow) {

    size_t len;
    
    if (filerow >= g_editor.numrows) {
        return;
    }
    
    if ((len = calculate_visible_line_length (filerow)) > 0) {
        buffer_append (ab, &g_editor.rows[filerow].chars[g_editor.coloff], len);
    }

}

void display_draw_single_row (append_buffer_t *ab, size_t y) {

    size_t filerow = y + g_editor.rowoff;
    
    if (filerow >= g_editor.numrows) {
        buffer_append (ab, "~", 1);
    } else {
        display_draw_file_row (ab, filerow);
    }
    
    buffer_append (ab, "\x1b[K", 3);
    buffer_append (ab, "\r\n", 2);

}

void display_draw_rows (append_buffer_t *ab) {

    size_t y;
    
    for (y = 0; y < g_editor.screen_rows - 2; y++) {
        display_draw_single_row (ab, y);
    }

}

void display_draw_status_bar (append_buffer_t *ab) {

    char status[80], rstatus[80];
    int len, rlen;
    
    buffer_append (ab, "\x1b[7m", 4);
    
    len = snprintf (status, sizeof (status), " %.20s - %zu lines %s",
        g_editor.filename ? g_editor.filename : "[No Name]",
            g_editor.numrows, g_editor.dirty ? "(modified)" : "");
    
    rlen = snprintf (rstatus, sizeof (rstatus), "%zu/%zu", g_editor.cy + 1, g_editor.numrows);
    
    if ((size_t) len > g_editor.screen_cols) {
        len = (int) g_editor.screen_cols;
    }
    
    buffer_append (ab, status, (size_t) len);
    
    while ((size_t) len < g_editor.screen_cols) {
    
        if (g_editor.screen_cols - (size_t) len == (size_t) rlen) {
        
            buffer_append (ab, rstatus, (size_t) rlen);
            break;
        
        }
        
        buffer_append (ab, " ", 1);
        len++;
    
    }
    
    buffer_append (ab, "\x1b[m", 3);
    buffer_append (ab, "\r\n", 2);

}

void display_draw_message_bar (append_buffer_t *ab) {

    char status[80];
    int len;
    
    buffer_append (ab, "\x1b[K", 3);
    
    len = snprintf (status, sizeof (status), "^O = Save     ^X = Exit");
    buffer_append (ab, status, len);
    
    while ((size_t) len < g_editor.screen_cols) {
    
        buffer_append (ab, " ", 1);
        len++;
    
    }

}

void display_scroll (void) {

    /* Vertical scrolling. */
    if (g_editor.cy < g_editor.rowoff) {
        g_editor.rowoff = g_editor.cy;
    }
    
    if (g_editor.cy >= g_editor.rowoff + g_editor.screen_rows - 2) {
        g_editor.rowoff = g_editor.cy - g_editor.screen_rows + 3;
    }
    
    /* Horizontal scrolling. */
    if (g_editor.cx < g_editor.coloff) {
        g_editor.coloff = g_editor.cx;
    }
    
    if (g_editor.cx >= g_editor.coloff + g_editor.screen_cols) {
        g_editor.coloff = g_editor.cx - g_editor.screen_cols + 1;
    }

}

void display_refresh_screen (void) {

    append_buffer_t ab = ABUF_INIT;
    char buf[32];
    
    display_scroll ();
    
    buffer_append (&ab, "\x1b[?25l", 6);                    /* Hide cursor */
    buffer_append (&ab, "\x1b[H", 3);                       /* Move cursor to home */
    
    display_draw_rows (&ab);
    display_draw_status_bar (&ab);
    display_draw_message_bar (&ab);
    
    snprintf (buf, sizeof (buf), "\x1b[%zu;%zuH",
        (g_editor.cy - g_editor.rowoff) + 1,
            (g_editor.cx - g_editor.coloff) + 1);
    
    buffer_append (&ab, buf, strlen (buf));
    buffer_append (&ab, "\x1b[?25h", 6);                    /* Show cursor */
    
    if (write (STDOUT_FILENO, ab.buffer, ab.length) == -1) {
        /* */
    }
    
    buffer_free (&ab);

}

/******************************************************************************
 * @file            editpr.c
 *****************************************************************************/
#include    <ctype.h>
#include    <fcntl.h>
#include    <stdarg.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>

#include    "buffer.h"
#include    "display.h"
#include    "editor.h"

editor_state_t g_editor = { 0 };

static int editor_allocate_new_row (editor_row_t *row, size_t len) {

    if (!(row->chars = malloc (len + 1))) {
        return 0;
    }
    
    row->size = len;
    return 1;

}

static int editor_reallocate_rows_array (void) {

    editor_row_t *new_rows = realloc (g_editor.rows, sizeof (editor_row_t) * (g_editor.numrows + 1));
    
    if (!new_rows) {
        return 0;
    }
    
    g_editor.rows = new_rows;
    return 1;

}

void editor_insert_row (size_t at, const char *str, size_t len) {

    if (at > g_editor.numrows || !str) {
        return;
    }
    
    if (!editor_reallocate_rows_array ()) {
        return;
    }
    
    memmove (&g_editor.rows[at + 1], &g_editor.rows[at], sizeof (editor_row_t) * (g_editor.numrows - at));
    
    if (!editor_allocate_new_row (&g_editor.rows[at], len)) {
        return;
    }
    
    memcpy (g_editor.rows[at].chars, str, len);
    g_editor.rows[at].chars[len] = '\0';
    
    g_editor.numrows++;
    g_editor.dirty++;

}

static void editor_row_insert_char (editor_row_t *row, size_t at, int ch) {

    char *new_chars;
    
    if (!row) {
        return;
    }
    
    if (at > row->size) {
        at = row->size;
    }
    
    if (!(new_chars = realloc (row->chars, row->size + 2))) {
        return;
    }
    
    memmove (&new_chars[at + 1], &new_chars[at], row->size - at + 1);
    row->chars = new_chars;
    
    row->chars[at] = ch;
    row->size++;
    
    g_editor.dirty++;

}

static void editor_insert_char (int ch) {

    if (g_editor.cy == g_editor.numrows) {
        editor_insert_row (g_editor.numrows, "", 0);
    }
    
    editor_row_insert_char (&g_editor.rows[g_editor.cy], g_editor.cx, ch);
    g_editor.cx++;

}

static void editor_split_current_row (void) {

    editor_row_t *row = &g_editor.rows[g_editor.cy];
    editor_insert_row (g_editor.cy + 1, &row->chars[g_editor.cx], row->size - g_editor.cx);
    
    row = &g_editor.rows[g_editor.cy];
    row->size = g_editor.cx;
    
    row->chars[row->size] = '\0';

}

static void editor_insert_newline (void) {

    if (g_editor.cx == 0) {
        editor_insert_row (g_editor.cy, "", 0);
    } else {
        editor_split_current_row ();
    }
    
    g_editor.cx = 0;
    g_editor.cy++;

}

static void editor_read_escape_sequence (char *seq) {

    seq[0] = getchar ();
    seq[1] = getchar ();

}

static void editor_snap_cursor_to_line_end (void) {

    editor_row_t *row = (g_editor.cy >= g_editor.numrows) ? 0 : &g_editor.rows[g_editor.cy];
    size_t row_len = row ? row->size : 0;
    
    if (g_editor.cx > row_len) {
        g_editor.cx = row_len;
    }

}

static void editor_row_free (editor_row_t *row) {

    if (row) {
    
        free (row->chars);
        
        row->chars = 0;
        row->size = 0;
    
    }

}

static void editor_delete_row (size_t at) {

    if (at >= g_editor.numrows) {
        return;
    }

    editor_row_free (&g_editor.rows[at]);
    
    memmove (&g_editor.rows[at], &g_editor.rows[at + 1], 
        sizeof (editor_row_t) * (g_editor.numrows - at - 1));
    
    g_editor.numrows--;
    g_editor.dirty++;

}

static int should_skip_delete (void) {
    return (g_editor.cy == g_editor.numrows) || (g_editor.cx == 0 && g_editor.cy == 0);
}

static void editor_row_delete_char (editor_row_t *row, size_t at) {

    if (!row || at >= row->size) {
        return;
    }

    memmove (&row->chars[at], &row->chars[at + 1], row->size - at);
    
    g_editor.dirty++;
    row->size--;

}

static void editor_delete_char_in_row (editor_row_t *row) {

    editor_row_delete_char (row, g_editor.cx - 1);
    g_editor.cx--;

}

static void editor_row_append_string (editor_row_t *row, const char *str, size_t len) {

    char *new_chars;
    
    if (!row || !str) {
        return;
    }
    
    new_chars = realloc(row->chars, row->size + len + 1);
    
    if (!new_chars) {
        return;
    }
    
    memcpy (&new_chars[row->size], str, len);
    row->chars = new_chars;
    
    row->size += len;
    row->chars[row->size] = '\0';
    
    g_editor.dirty++;

}

static void editor_merge_with_previous_row (editor_row_t *row) {

    g_editor.cx = g_editor.rows[g_editor.cy - 1].size;
    
    editor_row_append_string (&g_editor.rows[g_editor.cy - 1], row->chars, row->size);
    editor_delete_row (g_editor.cy);
    
    g_editor.cy--;

}

static int editor_delete_char (void) {

    editor_row_t *row;
    
    if (should_skip_delete ()) {
        return 0;
    }
    
    row = &g_editor.rows[g_editor.cy];
    
    if (g_editor.cx > 0) {
    
        editor_delete_char_in_row (row);
        return 1;
    
    }
    
    editor_merge_with_previous_row (row);
    return 2;

}

static int editor_handle_bracket_sequence (char seq[3]) {

    int ret = 0;
    
    switch (seq[1]) {
    
        case '3': {
        
            editor_row_t *row = (g_editor.cy >= g_editor.numrows) ? 0 : &g_editor.rows[g_editor.cy];
            
            if (row) {
            
                if (g_editor.cx < row->size) {
                
                    g_editor.cx++;
                    ret = editor_delete_char ();
                
                }
                
                if (g_editor.cx == row->size && g_editor.cy + 1 < g_editor.numrows) {
                
                    g_editor.cx = 0;
                    g_editor.cy++;
                    
                    ret = editor_delete_char ();
                
                }
            
            }
            
            break;
        
        }
        
        case '5': {
        
            size_t times = g_editor.screen_rows - 2;
            
            while (times--) {
            
                if (g_editor.cy > 0) {
                    g_editor.cy--;
                }
            
            }
            
            ret = 2;
            break;
        
        }
        
        case '6': {
        
            size_t times = g_editor.screen_rows - 2;
            
            while (times--) {
            
                if (g_editor.cy < g_editor.numrows) {
                    g_editor.cy++;
                }
            
            }
            
            ret = 2;
            break;
        
        }
        
        case 'A': {
        
            if (g_editor.cy > 0) {
            
                g_editor.cy--;
                ret = 2;
            
            }
            
            break;
        
        }
        
        case 'B': {
        
            if (g_editor.cy < g_editor.numrows) {
            
                g_editor.cy++;
                ret = 2;
            
            }
            
            break;
        
        }
        
        case 'C': {
        
            editor_row_t *row = (g_editor.cy >= g_editor.numrows) ? 0 : &g_editor.rows[g_editor.cy];
            
            if (row && g_editor.cx < row->size) {
            
                g_editor.cx++;
                
                ret = 1;
                break;
            
            }
            
            if (row && g_editor.cx == row->size) {
            
                g_editor.cx = 0;
                g_editor.cy++;
                
                ret = 2;
                break;
            
            }
            
            break;
        
        }
        
        case 'D': {
        
            if (g_editor.cx > 0) {
            
                g_editor.cx--;
                
                ret = 1;
                break;
            
            }
            
            if (g_editor.cy > 0) {
            
                g_editor.cy--;
                g_editor.cx = g_editor.rows[g_editor.cy].size;
                
                ret = 2;
                break;
            
            }
            
            break;
        
        }
        
        case 'F': {
        
            if (g_editor.cy < g_editor.numrows) {
                g_editor.cx = g_editor.rows[g_editor.cy].size;
            }
            
            ret = 1;
            break;
        
        }
        
        case 'H': {
        
            g_editor.cx = 0;
            
            ret = 1;
            break;
        
        }
        
        default: {
        
            return 0;
        
        }
    
    }
    
    editor_snap_cursor_to_line_end ();
    return ret;

}

static int editor_handle_escape_sequence (char seq[3]) {

    int ret = 0;
    
    if (seq[0] == '[') {
    
        if (seq[1] >= '0' && seq[1] <= '9') {
        
            seq[2] = getchar ();
            
            if (seq[2] != '~') {
                return 0;
            }
        
        }
        
        if ((ret = editor_handle_bracket_sequence (seq))) {
            return ret;
        }
    
    }
    
    return 0;

}

static int editor_get_confirmation (void) {

    char seq[3];
    int ch;
    
    while ((ch = getchar ()) != EOF) {
    
        if (ch == '\x18') {
            return 1;
        }
        
        if (ch == '\x1b') {
        
            seq[0] = getchar ();
            seq[1] = getchar ();
            
            if (seq[0] == '[' && (seq[1] >= '0' && seq[1] <= '9')) {
                seq[2] = getchar ();
            }
        
        }
        
        break;
    
    }
    
    return 0;

}

static int editor_handle_quit_attempt (void) {

    append_buffer_t ab = ABUF_INIT;
    
    char status[80], buf[32];
    int len;
    
    size_t i;
    
    if (g_editor.dirty) {
    
        buffer_append (&ab, "\x1b[?25l", 6);                /* Hide cursor */
        
        snprintf (buf, sizeof (buf), "\x1b[%zu;1H", g_editor.screen_rows);
        buffer_append (&ab, buf, strlen (buf));
        
        buffer_append (&ab, "\x1b[K", 3);
        
        len = snprintf (status, sizeof (status), "WARNING! File has unsaved changes. Press Ctrl-X 1 more time to quit.");
        buffer_append (&ab, status, len);
        
        while ((size_t) len < g_editor.screen_cols) {
        
            buffer_append (&ab, " ", 1);
            len++;
        
        }
        
        if (write (STDOUT_FILENO, ab.buffer, ab.length) == -1) {
            /* */
        }
        
        buffer_free (&ab);
        
        if (!editor_get_confirmation ()) {
        
            display_refresh_screen ();
            return 0;
        
        }
    
    }
    
    write (STDOUT_FILENO, "\x1b[2J", 4);
    write (STDOUT_FILENO, "\x1b[H", 3);
    
    write (STDOUT_FILENO, "\x1b[?25h", 6);                /* Show cursor */
    
    for (i = 0; i < g_editor.numrows; i++) {
        editor_delete_row (i);
    }
    
    return 1;

}

static void editor_write_filename (const char *promp,  char *path) {

    append_buffer_t ab = ABUF_INIT;
    
    char status[80], buf[32];
    size_t len, i;
    
    buffer_append (&ab, "\x1b[?25l", 6);                /* Hide cursor */
    
    snprintf (buf, sizeof (buf), "\x1b[%zu;1H", g_editor.screen_rows);
    buffer_append (&ab, buf, strlen (buf));
    
    buffer_append (&ab, "\x1b[K", 3);
    
    len = snprintf (status, sizeof (status), "%s%s", promp, path);
    buffer_append (&ab, status, len);
    
    for (i = len; i < g_editor.screen_cols; i++) {
        buffer_append (&ab, " ", 1);
    }
    
    snprintf (buf, sizeof (buf), "\x1b[%zu;%zuH", g_editor.screen_rows, len + 1);
    buffer_append (&ab, buf, strlen (buf));
    
    buffer_append (&ab, "\x1b[?25h", 6);                /* Show cursor */
    
    if (write (STDOUT_FILENO, ab.buffer, ab.length) == -1) {
        /* */
    }
    
    buffer_free (&ab);

}

static char *editor_prompt (const char *prompt) {

    char *buf, seq[3];
    int ch;
    
    size_t buf_len = 0;
    size_t buf_size = 4096;
    
    if (!(buf = malloc (buf_size))) {
        return 0;
    }
    
    memset (buf, 0, buf_size);
    
    for (;;) {
    
        editor_write_filename (prompt, buf);
        
        if ((ch = getchar ()) != EOF) {
        
            if (ch == 127) {
            
                if (buf_len > 0) { buf[--buf_len] = '\0'; }
                continue;
            
            }
            
            if (ch >= 0x20 && ch <= 0x7F) {
            
                if (buf_len < buf_size) {
                    buf[buf_len++] = ch;
                }
                
                continue;
            
            }
            
            if (ch == '\n' || ch == '\r') {
                return buf;
            }
            
            if (ch == '\x1b') {
            
                seq[0] = getchar ();
                seq[1] = getchar ();
                
                if (seq[0] == '[' && (seq[1] >= '0' && seq[1] <= '9')) {
                    seq[2] = getchar ();
                }
                
                continue;
            
            }
            
            free (buf);
            return 0;
        
        }
    
    }

}

static char *get_filename_for_save (void) {

    if (g_editor.filename) {
        return g_editor.filename;
    }
    
    g_editor.filename = editor_prompt ("Save as: ");
    
    if (!g_editor.filename) {
    
        append_buffer_t ab = ABUF_INIT;
        
        char status[80], buf[32];
        int len;
        
        buffer_append (&ab, "\x1b[?25l", 6);                /* Hide cursor */
        
        snprintf (buf, sizeof (buf), "\x1b[%zu;1H", g_editor.screen_rows);
        buffer_append (&ab, buf, strlen (buf));
        
        buffer_append (&ab, "\x1b[K", 3);
        
        len = snprintf (status, sizeof (status), "Save aborted");
        buffer_append (&ab, status, len);
        
        while ((size_t) len < g_editor.screen_cols) {
        
            buffer_append (&ab, " ", 1);
            len++;
        
        }
        
        snprintf (buf, sizeof (buf), "\x1b[%zu;%zuH", (g_editor.cy - g_editor.rowoff) + 1, (g_editor.cx - g_editor.coloff) + 1);
        buffer_append (&ab, buf, strlen (buf));
        
        buffer_append (&ab, "\x1b[?25h", 6);                /* Show cursor */
        
        if (write (STDOUT_FILENO, ab.buffer, ab.length) == -1) {
            /* */
        }
        
        buffer_free (&ab);
    
    }
    
    return g_editor.filename;

}

static void editor_save (void) {

    append_buffer_t ab = ABUF_INIT;
    
    char status[80], buf[32];
    int len, fd;
    
    size_t expected, i;
    ssize_t written;
    
    if (!get_filename_for_save ()) {
        return;
    }
    
    if ((fd = open (g_editor.filename, O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) {
    
        buffer_append (&ab, "\x1b[?25l", 6);                /* Hide cursor */
        
        snprintf (buf, sizeof (buf), "\x1b[%zu;1H", g_editor.screen_rows);
        buffer_append (&ab, buf, strlen (buf));
        
        buffer_append (&ab, "\x1b[K", 3);
        
        len = snprintf (status, sizeof (status), "Save failed");
        buffer_append (&ab, status, len);
        
        while ((size_t) len < g_editor.screen_cols) {
        
            buffer_append (&ab, " ", 1);
            len++;
        
        }
        
        snprintf (buf, sizeof (buf), "\x1b[%zu;%zuH", (g_editor.cy - g_editor.rowoff) + 1, (g_editor.cx - g_editor.coloff) + 1);
        buffer_append (&ab, buf, strlen (buf));
        
        buffer_append (&ab, "\x1b[?25h", 6);                /* Show cursor */
        
        if (write (STDOUT_FILENO, ab.buffer, ab.length) == -1) {
            /* */
        }
        
        buffer_free (&ab);
        return;
    
    }
    
    for (i = 0; i < g_editor.numrows; i++) {
    
        expected = g_editor.rows[i].size;
        
        if (expected > 0) {
        
            written = write (fd, g_editor.rows[i].chars, expected);
            
            if (written == (ssize_t) expected) {
            
                written = write (fd, "\r\n", 2);
                
                if (written == 2) {
                    continue;
                }
            
            }
        
        } else {
        
            expected = 2;
            written = write (fd, "\r\n", 2);
            
            if (written == 2) {
                continue;
            }
        
        }
        
        buffer_append (&ab, "\x1b[?25l", 6);                /* Hide cursor */
        
        snprintf (buf, sizeof (buf), "\x1b[%zu;1H", g_editor.screen_rows);
        buffer_append (&ab, buf, strlen (buf));
        
        buffer_append (&ab, "\x1b[K", 3);
        
        //len = snprintf (status, sizeof (status), "Save failed: write=%d expected=%u", (int) written, (unsigned) expected);
        len = snprintf (status, sizeof (status), "Save failed");
        buffer_append (&ab, status, len);
        
        while ((size_t) len < g_editor.screen_cols) {
        
            buffer_append (&ab, " ", 1);
            len++;
        
        }
        
        snprintf (buf, sizeof (buf), "\x1b[%zu;%zuH", (g_editor.cy - g_editor.rowoff) + 1, (g_editor.cx - g_editor.coloff) + 1);
        buffer_append (&ab, buf, strlen (buf));
        
        buffer_append (&ab, "\x1b[?25h", 6);                /* Show cursor */
        
        if (write (STDOUT_FILENO, ab.buffer, ab.length) == -1) {
            /* */
        }
        
        buffer_free (&ab);
        
        close (fd);
        for (;;) {}
    
    }
    
    g_editor.dirty = 0;
    close (fd);

}

void editor_run (void) {

    append_buffer_t ab = ABUF_INIT; 
    
    char seq[3], buf[32];
    int ch, ret;
    
    while ((ch = getchar ()) != EOF) {
    
        if (ch == '\x01') {
        
            seq[0] = '[';
            seq[1] = 'H';
            
            editor_handle_escape_sequence (seq);
            goto _update;
        
        }
        
        if (ch == '\x05') {
        
            seq[0] = '[';
            seq[1] = 'F';
            
            editor_handle_escape_sequence (seq);
            goto _update;
        
        }
        
        if (ch == '\x0B') {
        
            seq[0] = '[';
            seq[1] = '3';
            
            editor_handle_escape_sequence (seq);
            goto _update;
        
        }
        
        if (ch == '\x0F') {
        
            editor_save ();
            
            display_refresh_screen ();
            continue;
        
        }
        
        if (ch == '\x15') {
            continue;
        }
        
        if (ch == '\x17') {
            continue;
        }
        
        if (ch == '\x18') {
        
            if (editor_handle_quit_attempt ()) {
                return;
            }
            
            continue;
        
        }
        
        if (ch == '\x1b') {
        
            editor_read_escape_sequence (seq);
            
            if ((ret = editor_handle_escape_sequence (seq)) == 1) {
                goto _update;
            }
            
            if (ret == 2) {
                display_refresh_screen ();
            }
            
            continue;
        
        }
        
        switch (ch) {
        
            case '\n':  case '\r':
            
                editor_insert_newline ();
                display_refresh_screen ();
                
                continue;
            
            case 127:
            
                if ((ret = editor_delete_char ()) == 1) {
                    goto _update;
                }
                
                if (ret == 2) {
                    display_refresh_screen ();
                }
                
                continue;
            
            default:
            
                break;
        
        }
        
        if (ch < 0x20 || ch > 0x7F) {
            continue;
        }
        
        editor_insert_char (ch);
        
    _update:
        
        display_scroll ();
        buffer_append (&ab, "\x1b[?25l", 6);                /* Hide cursor */
        
        snprintf (buf, sizeof (buf), "\x1b[%zu;1H", (g_editor.cy - g_editor.rowoff) + 1);
        
        buffer_append (&ab, buf, strlen (buf));
        buffer_append (&ab, "\x1b[K", 3);
        
        display_draw_file_row (&ab, g_editor.cy);
        
        snprintf (buf, sizeof (buf), "\x1b[%zu;1H", g_editor.screen_rows - 1);
        buffer_append (&ab, buf, strlen (buf));
        
        display_draw_status_bar (&ab);
        
        snprintf (buf, sizeof (buf), "\x1b[%zu;%zuH", (g_editor.cy - g_editor.rowoff) + 1, (g_editor.cx - g_editor.coloff) + 1);
        buffer_append (&ab, buf, strlen (buf));
        
        buffer_append (&ab, "\x1b[?25h", 6);                /* Show cursor */
        
        if (write (STDOUT_FILENO, ab.buffer, ab.length) == -1) {
            /* */
        }
        
        buffer_free (&ab);
    
    }

}

void editor_init (void) {

    memset (&g_editor, 0, sizeof (g_editor));
    
    g_editor.screen_cols = 80;
    g_editor.screen_rows = 24;

}


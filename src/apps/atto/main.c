/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <sys/ioctl.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <termios.h>
#include    <unistd.h>

#include    "display.h"
#include    "editor.h"
#include    "ll.h"

static struct termios terminal_original_input = { 0 };
static int terminal_input_mode_changed = 0;

static int terminal_get_window_size (size_t *rows, size_t *cols) {

    struct winsize ws;

    if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    }

    *cols = ws.ws_col;
    *rows = ws.ws_row;
    
    return 0;

}

/******************************************************************************
 * @function        initialize_terminal_input
 *****************************************************************************/
static void initialize_terminal_input (void) {

    struct termios mode;
    
    if (!isatty (STDIN_FILENO) || tcgetattr (STDIN_FILENO, &mode) != 0) {
        return;
    }
    
    terminal_original_input = mode;
    
    mode.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    mode.c_iflag &= ~(IXON | ICRNL);
    
    mode.c_cc[VMIN] = 1;
    mode.c_cc[VTIME] = 0;
    
    if (tcsetattr (STDIN_FILENO, TCSANOW, &mode) == 0) {
        terminal_input_mode_changed = 1;
    }

}

/******************************************************************************
 * @function        restore_terminal_input
 *****************************************************************************/
static void restore_screen (void) {

    printf ("\033[?1049l");
    fflush (stdout);

}

/******************************************************************************
 * @function        restore_terminal_input
 *****************************************************************************/
static void restore_terminal_input (void) {

    if (terminal_input_mode_changed) {
        tcsetattr (STDIN_FILENO, TCSANOW, &terminal_original_input);
    }

}

/******************************************************************************
 * @function        strip_line_endings
 *****************************************************************************/
static void strip_line_endings (const char *line, ssize_t *linelen) {

    while (*linelen > 0 && (line[*linelen - 1] == '\n' || line[*linelen - 1] == '\r')) {
        (*linelen)--;
    }

}

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {

    int i;
    
    (void) argc;
    (void) argv;
    
    printf ("\033[?1049h");
    fflush (stdout);
    
    atexit (restore_terminal_input);
    atexit (restore_screen);
    
    initialize_terminal_input ();
    editor_init ();
    
    if (terminal_get_window_size (&g_editor.screen_rows, &g_editor.screen_cols) == -1) {
    
        g_editor.screen_cols = 80;
        g_editor.screen_rows = 24;
    
    }
    
    if (argc > 1) {
    
        g_editor.multi_file = 1;
        
        for (i = 1; i < argc; i++) {
        
            FILE *fp;
            
            char *line, *line_end, *real_line;
            unsigned long real_line_len;
            
            unsigned long new_line_number = 1;
            unsigned long newlines;
            
            void *load_line_internal_data = NULL;
            
            if (access (argv[i], F_OK) == 0) {
            
                if (!(fp = fopen (argv[i], "r"))) {
                    return 1;
                }
                
                load_line_internal_data = load_line_create_internal_data (&new_line_number);
                
                while (!load_line (&line, &line_end, &real_line, &real_line_len, &newlines, fp, &load_line_internal_data)) {
                
                    strip_line_endings (real_line, &real_line_len);
                    editor_insert_row (g_editor.numrows, real_line, real_line_len);
                
                }
                
                load_line_destroy_internal_data (load_line_internal_data);
                
                if (g_editor.filename) {
                    free (g_editor.filename);
                }
            
            }
            
            g_editor.filename = strdup (argv [i]);
            g_editor.dirty = 0;
            
            display_refresh_screen ();
            editor_run ();
        
        }
        
        return 0;
    
    }
    
    display_refresh_screen ();
    editor_run ();
    
    return 0;

}

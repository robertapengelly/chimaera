/******************************************************************************
 * @file            report.c
 *****************************************************************************/
#include    <stdarg.h>
#include    <stdio.h>
#include    <string.h>

#include    "report.h"

unsigned long errors = 0;

static void reset_console_color (void) {
    fprintf (stderr, "\033[0m");
}

static void set_console_color (int color) {
    fprintf (stderr, "\033[%dm", color);
}

static void output_message (const char *filename, unsigned long lineno, unsigned long idx, enum report_type type, const char *fmt, va_list ap) {

    if (filename) {
    
        if (lineno == 0 && idx == 0) {
            fprintf (stderr, "%s: ", filename);
        } else {
            fprintf (stderr, "%s:", filename);
        }
    
    }
    
    if (lineno > 0) {
    
        if (idx == 0) {
            fprintf (stderr, "%lu: ", lineno);
        } else {
            fprintf (stderr, "%lu:", lineno);
        }
    
    }
    
    if (idx > 0) {
        fprintf (stderr, "%lu: ", idx);
    }
    
    if (type == REPORT_ERROR || type == REPORT_FATAL_ERROR) {
    
        set_console_color (COLOR_ERROR);
        
        if (type == REPORT_ERROR) {
            fprintf (stderr, "error:");
        } else {
            fprintf (stderr, "fatal error:");
        }
    
    } else if (type == REPORT_INTERNAL_ERROR) {
    
        set_console_color (COLOR_INTERNAL_ERROR);
        fprintf (stderr, "internal error:");
    
    } else if (type == REPORT_WARNING) {
    
        set_console_color (COLOR_WARNING);
        fprintf (stderr, "warning:");
    
    }
    
    reset_console_color ();
    fprintf (stderr, " ");
    
    vfprintf (stderr, fmt, ap);
    fprintf (stderr, "\n");
    
    if (type != REPORT_WARNING) {
        ++errors;
    }

}

unsigned long get_error_count (void) {
    return errors;
}

void report_at (const char *filename, unsigned long lineno, enum report_type type, const char *fmt, ...) {

    va_list ap;
    
    va_start (ap, fmt);
    output_message (filename, lineno, 0, type, fmt, ap);
    va_end (ap);

}

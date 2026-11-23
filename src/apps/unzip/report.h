/******************************************************************************
 * @file            report.h
 *****************************************************************************/
#ifndef     _REPORT_H
#define     _REPORT_H

enum report_type {

    REPORT_ERROR = 0,
    REPORT_FATAL_ERROR,
    REPORT_INTERNAL_ERROR,
    REPORT_WARNING

};

#define     COLOR_ERROR                 91
#define     COLOR_INTERNAL_ERROR        94
#define     COLOR_WARNING               95

unsigned long get_error_count (void);
void report_at (const char *filename, unsigned long line_number, enum report_type type, const char *fmt, ...);

#endif      /* _REPORT_H */

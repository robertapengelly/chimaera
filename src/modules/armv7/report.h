/******************************************************************************
 * @file            report.h
 *****************************************************************************/
#ifndef     _REPORT_H
#define     _REPORT_H

#define     COLOR_ERROR                 91
#define     COLOR_INTERNAL_ERROR        94
#define     COLOR_WARNING               95

#define     REPORT_WARNING              0
#define     REPORT_ERROR                1
#define     REPORT_FATAL_ERROR          3
#define     REPORT_INTERNAL_ERROR       4

unsigned int get_error_count (void);
void report_at (const char *filename, unsigned long lineno, int type, const char *fmt, ...);

#endif      /* _REPORT_H */

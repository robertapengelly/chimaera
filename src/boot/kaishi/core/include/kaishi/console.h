/******************************************************************************
 * @file            console.h
 *****************************************************************************/
#ifndef     _CONSOLE_H
#define     _CONSOLE_H

void console_highlight (int x, int y, int padding);

void console_get_cursor (int *x, int *y);
void console_clear (void);

void console_reset_colors (void);

void console_hide_cursor (void);
void console_show_cursor (void);

void console_clear_line (int x, int y);

#endif      /* _CONSOLE_H */

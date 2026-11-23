/******************************************************************************
 * @file            display.h
 *****************************************************************************/
#ifndef     _DISPLAY_H
#define     _DISPLAY_H

#include    "types.h"

void display_draw_single_row (append_buffer_t *ab, size_t y);
void display_draw_file_row (append_buffer_t *ab, size_t filerow);
void display_draw_rows (append_buffer_t *ab);

void display_draw_status_bar (append_buffer_t *ab);
void display_draw_message_bar (append_buffer_t *ab);

void display_scroll (void);
void display_refresh_screen (void);

#endif      /* _DISPLAY_H */

/******************************************************************************
 * @file            types.h
 *****************************************************************************/
#ifndef     _TYPES_H
#define     _TYPES_H

#include    <stddef.h>
#include    <time.h>

#define     CTRL_KEY(k)                 ((k) & 0x1f)

#define     NANO_VERION                 "0.1.0"
#define     NANO_QUIT_TIMES             1

#define     STATUS_MSG_TIMEOUT          5
#define     MIN_BUFFER_SIZE             128

typedef enum {

    KEY_NULL = 0,
    KEY_BACKSPACE = 127,
    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_DEL

} editor_key_t;

typedef struct {

    size_t size;
    char *chars;

} editor_row_t;

typedef struct {

    size_t cx, cy;
    
    size_t rowoff, coloff;
    size_t screen_rows, screen_cols;
    
    size_t numrows;
    
    editor_row_t *rows;
    int dirty;
    
    time_t status_msg_time;
    char *filename;
    
    int multi_file;

} editor_state_t;

typedef struct {

    char *buffer;
    size_t length;

} append_buffer_t;

#define     ABUF_INIT                   { NULL, 0 }

#endif      /* _TYPES_H */

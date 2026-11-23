/******************************************************************************
 * @file            stdio.h
 *****************************************************************************/
#ifndef     _STDIO_H
#define     _STDIO_H

#include    <sys/types.h>
#include    <stdarg.h>

#define     __NEED_SIZE_T
#define     __NEED_NULL
#include    <stddef.h>

typedef struct _IO_FILE {

    ssize_t (*_impl_write) (void *buf, size_t len, void *stream);
    ssize_t (*_impl_read) (void *buf, size_t len, void *stream);
    
    int (*_impl_error) (FILE *stream);
    int (*_impl_eof) (FILE *stream);
    
    int _fileno;
    int _flags;
    int _mode;
    
    char *_buffer;
    
    size_t _buffer_size;
    size_t _buffer_pos;
    size_t _buffer_len;
    
    int _buffer_owned;
    int _buffer_reading;
    
    int _unget_character;
    int _unget_pending;
    
    struct _IO_FILE *_prev;
    struct _IO_FILE *_next;

} FILE;

FILE *__iob_func (void);

#define     stdin                      (__iob_func ())
#define     stdout                     (__iob_func () + 1)
#define     stderr                     (__iob_func () + 2)

#define     SEEK_SET                    0                   /* Seek from beginning of file.  */
#define     SEEK_CUR                    1                   /* Seek from current position.  */
#define     SEEK_END                    2                   /* Seek from end of file.  */

#define     _IOFBF                      1                   /* full buffering */
#define     _IOLBF                      2                   /* line buffering */
#define     _IONBF                      3                   /* no buffering */

#define     EXIT_SUCCESS                0
#define     EXIT_FAILURE                1

#define     EOF                         -1

int vfprintf (FILE *stream, const char *fmt, va_list ap);
int fprintf (FILE *stream, const char *fmt, ...);
int printf (const char *fmt, ...);

int vsnprintf (char *buf, size_t size, const char *fmt, va_list ap);
int snprintf (char *buf, size_t size, const char *fmt, ...);
int sprintf (char *s, const char *fmt, ...);

int setvbuf (FILE *stream, char *buf, int mode, size_t size);
int fputs (const char *s, FILE *stream);

int fputc (int ch, FILE *stream);
int fgetc (FILE *stream);

int getc (FILE *stream);

int putchar (int ch);
int getchar (void);

void clearerr (FILE *stream);
int ungetc (int ch, FILE *stream);

size_t fwrite (const void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fread (const void *ptr, size_t size, size_t nmemb, FILE *stream);

int fileno (FILE *stream);
int fflush (FILE *stream);
int ferror (FILE *stream);

FILE *fopen (const char *path, const char *mode);
int fclose (FILE *stream);

int rename (const char *old_path, const char *new_path);
int remove (const char *path);
int feof (FILE *stream);

void rewind (FILE *stream);
int fseek (FILE *stream, long offset, int whence);

long ftell (FILE *stream);

#endif      /* _STDIO_H */

/******************************************************************************
 * @file            file.c
 *****************************************************************************/
#include    <fcntl.h>
#include    <errno.h>
#include    <stdarg.h>
#include    <stddef.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>

#ifndef     O_BINARY
# define        O_BINARY                (1U << 3)
#endif

#define     _DEFAULT_BUFSIZE            1024
static FILE _iob[3] = { 0 };

FILE *__iob_func (void) {
    return _iob;
}

char _stdin_buf[_DEFAULT_BUFSIZE];
char _stdout_buf[_DEFAULT_BUFSIZE];
char _stderr_buf[_DEFAULT_BUFSIZE];

#define     G_FILE_FLAG_EOF             (1U << 26)          /* end of file reached */
#define     G_FILE_FLAG_ERROR           (1U << 27)          /* when a file error has occured */

static ssize_t __stdio_impl_write (const void *buf, size_t len, FILE *stream) {
    return write (stream->_fileno, buf, len);
}

static ssize_t __stdio_impl_read (const void *buf, size_t len, FILE *stream) {
    return read (stream->_fileno, buf, len);
}

static int __stdio_impl_eof (FILE *stream) {
    return (stream->_flags & G_FILE_FLAG_EOF) ? 1 : 0;
}

static int __stdio_impl_error (FILE *stream) {
    return (stream->_flags & G_FILE_FLAG_ERROR) ? 1 : 0;
}

FILE *__open_file_list = 0;
static void __open_file_list_initialize (void) {}

static void __open_file_list_add (FILE *file) {

    //__open_file_list_lock ();
    
    if (__open_file_list) {
    
        __open_file_list->_prev = file;
        file->_next = __open_file_list;
    
    }
    
    __open_file_list = file;
    
    //__open_file_list_unlock ();

}

static int __parse_mode_flags (const char *mode) {

    int flags = 0;
    
    switch (*mode) {
    
        case 'r':
        
            flags = O_RDONLY;
            break;
        
        case 'w':
        
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            break;
        
        case 'a':
        
            flags = O_WRONLY | O_CREAT | O_APPEND;
            break;
        
        default:
        
            errno = EINVAL;
            return EOF;
    
    }
    
    while (*(++mode)) {
    
        switch (*mode) {
    
            case '+':
            
                flags |= O_RDWR;
                break;
            
            case 't':
            
                flags &= ~O_BINARY;
                break;
            
            case 'b':
            
                flags |= O_BINARY;
                break;
            
            case 'x':
            
                flags |= O_EXCL;
                break;
            
            default:
            
                errno = EINVAL;
                return EOF;
        
        }
    
    }
    
    return flags;

}

static int __fdopen_static (int fd, const char *mode, FILE *file) {

    int flags = __parse_mode_flags (mode);
    
    if (flags == EOF) {
        return EOF;
    }
    
    //file->_lock = g_mutex_initialize ();
    
    file->_fileno = fd;
    file->_flags = flags;
    
    file->_mode = _IONBF;
    
    file->_impl_eof   = __stdio_impl_eof;
    file->_impl_error = __stdio_impl_error;
    file->_impl_read  = __stdio_impl_read;
    file->_impl_write = __stdio_impl_write;
    
    __open_file_list_add (file);
    return 0;

}

void clearerr (FILE *stream) {

    if (stream) {
        stream->_flags &= ~(G_FILE_FLAG_EOF | G_FILE_FLAG_ERROR);
    }

}

int fclose (FILE *stream) {

    int ret;
    
    if (!stream) {
        return EOF;
    }
    
    ret = fflush (stream);
    
    if (close (stream->_fileno) < 0) {
        ret = EOF;
    }
    
    if (stream->_prev) {
        stream->_prev->_next = stream->_next;
    } else {
        __open_file_list = stream->_next;
    }
    
    if (stream->_next) {
        stream->_next->_prev = stream->_prev;
    }
    
    if (stream->_buffer_owned) {
        free (stream->_buffer);
    }
    
    if (stream != stdin && stream != stdout && stream != stderr) {
        free (stream);
    }
    
    return ret;

}

int feof (FILE *stream) {

    int res = 0;
    
    if (stream->_impl_eof) {
        res = stream->_impl_eof (stream);
    }
    
    return res;

}

int ferror (FILE *stream) {

    int res = 0;
    
    if (stream->_impl_error) {
        res = stream->_impl_error (stream);
    }
    
    return res;

}

int fflush (FILE *stream) {

    FILE *file;
    
    size_t done;
    ssize_t written;
    
    int ret;
    
    if (!stream) {
    
        ret = 0;
        
        for (file = __open_file_list; file; file = file->_next) {
        
            if (!file->_buffer_reading && fflush (file) == EOF) {
                ret = EOF;
            }
        
        }
        
        return ret;
    
    }
    
    if (stream->_mode == _IONBF || !stream->_buffer) {
        return 0;
    }
    
    if (stream->_buffer_reading) {
    
        if (stream->_buffer_pos < stream->_buffer_len) {
        
            if (lseek (stream->_fileno, -((off_t) (stream->_buffer_len - stream->_buffer_pos)), SEEK_CUR) < 0) {
            
                stream->_flags |= G_FILE_FLAG_ERROR;
                return EOF;
            
            }
        
        }
        
        stream->_buffer_pos = 0;
        stream->_buffer_len = 0;
        
        return 0;
    
    }
    
    done = 0;
    
    while (done < stream->_buffer_pos) {
    
        written = stream->_impl_write (stream->_buffer + done, stream->_buffer_pos - done, stream);
        
        if (written <= 0) {
        
            if (done) {
            
                memmove (stream->_buffer, stream->_buffer + done, stream->_buffer_pos - done);
                stream->_buffer_pos -= done;
            
            }
            
            stream->_flags |= G_FILE_FLAG_ERROR;
            return EOF;
        
        }
        
        done += written;
    
    }
    
    stream->_buffer_pos = 0;
    return 0;

}

int fileno (FILE *stream) {

    if (!stream) {
    
        errno = EBADF;
        return -1;
    
    }
    
    return stream->_fileno;

}

int setvbuf (FILE *stream, char *buf, int mode, size_t size) {

    char *buffer = buf;
    int owned = 0;
    
    if (!stream || (mode != _IONBF && mode != _IOFBF && mode != _IOLBF)) {
    
        errno = EINVAL;
        return -1;
    
    }
    
    if (mode != _IONBF) {
    
        if (!size) {
        
            errno = EINVAL;
            return -1;
        
        }
        
        if (!buffer) {
        
            if (!(buffer = malloc (size))) {
            
                errno = ENOMEM;
                return -1;
            
            }
            
            owned = 1;
        
        }
    
    } else {
        buffer = NULL;
    }
    
    if (fflush (stream) == EOF) {
    
        if (owned) {
            free (buffer);
        }
        
        return -1;
    
    }
    
    if (stream->_buffer_owned) {
        free (stream->_buffer);
    }
    
    stream->_mode = mode;
    stream->_buffer = buffer;
    stream->_buffer_size = mode == _IONBF ? 0 : size;
    stream->_buffer_pos = 0;
    stream->_buffer_len = 0;
    stream->_buffer_owned = owned;
    stream->_buffer_reading = 0;
    
    return 0;

}

FILE *fopen (const char *path, const char *mode) {

    FILE *file;
    
    int flags;
    int fd;
    
    if (!(file = malloc (sizeof (*file)))) {
        return NULL;
    }
    
    memset (file, 0, sizeof (*file));
    flags = __parse_mode_flags (mode);
    
    if (flags == EOF) {
    
        free (file);
        return NULL;
    
    }
    
    if ((fd = (flags & O_CREAT) ? open (path, flags, 0666) : open (path, flags)) < 0) {
    
        free (file);
        return NULL;
    
    }
    
    if (!__fdopen_static (fd, mode, file)) {
        return file;
    }
    
    close (fd);
    free (file);
    
    return NULL;

}

size_t fread (const void *ptr, size_t size, size_t nmemb, FILE *stream) {

    size_t total, done, available;
    
    uint8_t *pos;
    ssize_t read;
    
    if (size == 0 || nmemb == 0) {
        return 0;
    }
    
    if (!(stream->_flags & (O_RDONLY | O_RDWR))) {
    
        stream->_flags |= G_FILE_FLAG_ERROR;
        return 0;
    
    }
    
    if (stream->_mode == _IONBF) {
    
        if (!stream->_impl_read || size > ((size_t) -1) / nmemb) {
        
            stream->_flags |= G_FILE_FLAG_ERROR;
            return 0;
        
        }
        
        total = size * nmemb;
        done = 0;
        
        if (stream->_unget_pending) {
        
            ((uint8_t *) ptr)[done++] = (uint8_t) stream->_unget_character;
            stream->_unget_pending = 0;
        
        }
        
        while (done < total) {
        
            pos = &(((uint8_t *) ptr)[done]);
            
            if (!(read = stream->_impl_read (pos, total - done, stream))) {
            
                stream->_flags |= G_FILE_FLAG_EOF;
                break;
            
            }
            
            if (read < 0) {
            
                stream->_flags |= G_FILE_FLAG_ERROR;
                break;
            
            }
            
            done += read;
        
        }
        
        return done / size;
    
    }
    
    if (stream->_buffer && (stream->_mode == _IOFBF || stream->_mode == _IOLBF)) {
    
        if (!stream->_impl_read || size > ((size_t) -1) / nmemb) {
        
            stream->_flags |= G_FILE_FLAG_ERROR;
            return 0;
        
        }
        
        if (!stream->_buffer_reading) {
        
            if (fflush (stream) == EOF) {
                return 0;
            }
            
            stream->_buffer_reading = 1;
            stream->_buffer_pos = 0;
            stream->_buffer_len = 0;
        
        }
        
        total = size * nmemb;
        done = 0;
        
        if (stream->_unget_pending) {
        
            ((uint8_t *) ptr)[done++] = (uint8_t) stream->_unget_character;
            stream->_unget_pending = 0;
        
        }
        
        while (done < total) {
        
            if (stream->_buffer_pos == stream->_buffer_len) {
            
                read = stream->_impl_read (stream->_buffer, stream->_buffer_size, stream);
                
                if (read <= 0) {
                
                    stream->_flags |= read == 0 ? G_FILE_FLAG_EOF : G_FILE_FLAG_ERROR;
                    break;
                
                }
                
                stream->_buffer_pos = 0;
                stream->_buffer_len = read;
            
            }
            
            pos = &(((uint8_t *) ptr)[done]);
            available = total - done;
            
            if (available > stream->_buffer_len - stream->_buffer_pos) {
                available = stream->_buffer_len - stream->_buffer_pos;
            }
            
            memcpy (pos, stream->_buffer + stream->_buffer_pos, available);
            stream->_buffer_pos += available;
            
            done += available;
        
        }
        
        return done / size;
    
    }
    
    stream->_flags |= G_FILE_FLAG_ERROR;
    return 0;

}

int fseek (FILE *stream, long offset, int whence) {

    if (!stream) {
    
        errno = EINVAL;
        return -1;
    
    }
    
    if (whence == SEEK_CUR && stream->_unget_pending) {
        offset--;
    }
    
    if (fflush (stream) == EOF || lseek (stream->_fileno, (off_t) offset, whence) < 0) {
        return -1;
    }
    
    stream->_flags &= ~G_FILE_FLAG_EOF;
    stream->_unget_pending = 0;
    
    return 0;

}

long ftell (FILE *stream) {

    long pos;
    
    if (!stream) {
    
        errno = EINVAL;
        return -1L;
    
    }
    
    pos = (long) lseek (stream->_fileno, 0, SEEK_CUR);
    
    if (pos < 0) {
        return -1L;
    }
    
    if (stream->_mode != _IONBF && stream->_buffer) {
    
        if (stream->_buffer_reading) {
            pos -= stream->_buffer_len - stream->_buffer_pos;
        } else {
            pos += stream->_buffer_pos;
        }
    
    }
    
    if (stream->_unget_pending) {
        pos--;
    }
    
    return pos;

}

size_t fwrite (const void *ptr, size_t size, size_t nmemb, FILE *stream) {

    uint8_t /**buffer, */*pos;
    ssize_t written;
    
    size_t total, done/*, off, e, b*/;
    
    if (size == 0 || nmemb == 0) {
        return 0;
    }
    
    if (!(stream->_flags & (O_WRONLY | O_RDWR))) {
    
        stream->_flags |= G_FILE_FLAG_ERROR;
        return 0;
    
    }
    
    if (stream->_mode == _IONBF) {
    
        if (!stream->_impl_write || size > ((size_t) -1) / nmemb) {
        
            stream->_flags |= G_FILE_FLAG_ERROR;
            return 0;
        
        }
        
        total = size * nmemb;
        done = 0;
        
        while (done < total) {
        
            pos = &(((uint8_t *) ptr)[done]);
            
            if ((written = stream->_impl_write (pos, total - done, stream)) <= 0) {
            
                stream->_flags |= G_FILE_FLAG_ERROR;
                break;
            
            }
            
            done += written;
        
        }
        
        return done / size;
    
    }
    
    if (stream->_buffer && (stream->_mode == _IOFBF || stream->_mode == _IOLBF)) {
    
        if (!stream->_impl_write || size > ((size_t) -1) / nmemb) {
        
            stream->_flags |= G_FILE_FLAG_ERROR;
            return 0;
        
        }
        
        if (stream->_buffer_reading) {
        
            if (fflush (stream) == EOF) {
                return 0;
            }
            
            stream->_buffer_reading = 0;
            stream->_buffer_pos = 0;
            stream->_buffer_len = 0;
        
        }
        
        total = size * nmemb;
        done = 0;
        
        while (done < total) {
        
            stream->_buffer[stream->_buffer_pos++] = ((const char *) ptr)[done++];
            
            if (stream->_buffer_pos == stream->_buffer_size || (stream->_mode == _IOLBF && ((const char *) ptr)[done - 1] == '\n')) {
            
                if (fflush (stream) == EOF) {
                    break;
                }
            
            }
        
        }
        
        return done / size;
    
    }
    
    stream->_flags |= G_FILE_FLAG_ERROR;
    return 0;

}

int fgetc (FILE *stream) {

    unsigned char ch = 0;
    
    if (fread (&ch, 1, 1, stream) != 1) {
        return EOF;
    }
    
    return ch;

}

int fputc (int ch, FILE *stream) {

    unsigned char c = (unsigned char) ch;
    
    if (fwrite (&c, 1, 1, stream) != 1) {
        return EOF;
    }
    
    return c;

}

int fputs (const char *s, FILE *stream) {

    size_t length = strlen (s);
    
    if (fwrite (s, 1, length, stream) != length) {
        return EOF;
    }
    
    return 0;

}

int getc (FILE *stream) {
    return fgetc (stream);
}

int getchar (void) {
    return fgetc (stdin);
}

int putchar (int ch) {
    return fputc (ch, stdout);
}

void rewind (FILE *stream) {

    if (!stream) {
        return;
    }
    
    fseek (stream, 0L, SEEK_SET);
    stream->_flags &= ~(G_FILE_FLAG_EOF | G_FILE_FLAG_ERROR);

}

int ungetc (int ch, FILE *stream) {

    if (!stream || ch == EOF || stream->_unget_pending) {
        return EOF;
    }
    
    stream->_unget_character = (unsigned char) ch;
    stream->_unget_pending = 1;
    
    stream->_flags &= ~G_FILE_FLAG_EOF;
    return (unsigned char) ch;

}

void __init_stdio (void) {

    __open_file_list_initialize ();
    
    memset (stdin, 0, sizeof (FILE));
    __fdopen_static (STDIN_FILENO, "r", stdin);
//    setvbuf (stdin, _stdin_buf, _IOLBF, _DEFAULT_BUFSIZE);
    
    memset (stdout, 0, sizeof (FILE));
    __fdopen_static (STDOUT_FILENO, "w", stdout);
//    setvbuf (stdout, _stdout_buf, _IOLBF, _DEFAULT_BUFSIZE);
    
    memset (stderr, 0, sizeof (FILE));
    __fdopen_static (STDERR_FILENO, "w", stderr);
//    setvbuf (stdin, _stderr_buf, _IONBF, _DEFAULT_BUFSIZE);

}


#define     PRINTF_BUFFER_SIZE          512

struct callback {

    FILE *stream;
    int error;
    
    char *buffer;
    size_t length;

};

int xvasprintf (int (*callback) (struct callback *, char), void *user_data, const char *fmt, va_list ap);

static void flush_printf (struct callback *callback) {

    if (callback->error || !callback->length) {
        return;
    }
    
    if (fwrite (callback->buffer, 1, callback->length, callback->stream) != callback->length) {
        callback->error = 1;
    }
    
    callback->length = 0;

}

static int cb_printf (struct callback *callback, char ch) {

    if (callback->error) {
        return EOF;
    }
    
    callback->buffer[callback->length] = ch;
    callback->length++;
    
    if (callback->length == PRINTF_BUFFER_SIZE) {
        flush_printf (callback);
    }
    
    return callback->error ? EOF : 1;

}

int vfprintf (FILE *stream, const char *fmt, va_list ap) {

    struct callback callback = { 0 };
    
    char buffer[PRINTF_BUFFER_SIZE];
    int ret;
    
    callback.stream = stream;
    callback.buffer = buffer;
    
    ret = xvasprintf (cb_printf, &callback, fmt, ap);
    flush_printf (&callback);
    
    return callback.error ? EOF : ret;

}

int fprintf (FILE *stream, const char *fmt, ...) {

    va_list ap;
    int ret;
    
    va_start (ap, fmt);
    ret = vfprintf (stream, fmt, ap);
    va_end (ap);
    
    return ret;

}

int printf (const char *fmt, ...) {

    va_list ap;
    int ret;
    
    va_start (ap, fmt);
    ret = vfprintf (stdout, fmt, ap);
    va_end (ap);
    
    return ret;

}

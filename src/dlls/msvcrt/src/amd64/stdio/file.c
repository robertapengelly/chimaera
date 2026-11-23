/******************************************************************************
 * @file            file.c
 *****************************************************************************/
#include    <sys/types.h>

#include    <errno.h>
#include    <fcntl.h>
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

#define     GENERIC_READ                                    0x80000000U
#define     GENERIC_WRITE                                   0x40000000U
#define     FILE_APPEND_DATA                                0x00000004U

#define     FILE_OPEN                                       1
#define     FILE_CREATE                                     2
#define     FILE_OPEN_IF                                    3
#define     FILE_OVERWRITE                                  4
#define     FILE_OVERWRITE_IF                               5

#define     SYNCHRONIZE                                     0x00100000U

#define     FILE_SHARE_READ                                 0x00000001U
#define     FILE_SHARE_WRITE                                0x00000002U
#define     FILE_SYNCHRONOUS_IO_NONALERT                    0x00000020U
#define     FILE_NON_DIRECTORY_FILE                         0x00000040U

typedef struct {

    uint16_t Length;
    uint16_t MaximumLength;
    uint16_t *Buffer;

} UNICODE_STRING;

typedef struct {

    uint32_t Length;
    void *RootDirectory;
    
    UNICODE_STRING *ObjectName;
    uint32_t Attributes;
    
    void *SecurityDescriptor;
    void *SecurityQualityOfService;

} OBJECT_ATTRIBUTES;

typedef struct {

    int32_t Status;
    uintptr_t Information;

} IO_STATUS_BLOCK;

#define     FileStandardInformation     5
#define     FilePositionInformation     14

typedef struct {
    int64_t CurrentByteOffset;
} FILE_POSITION_INFORMATION;

typedef struct {

    int64_t AllocationSize;
    int64_t EndOfFile;
    uint32_t NumberOfLinks;
    uint8_t DeletePending;
    uint8_t Directory;

} FILE_STANDARD_INFORMATION;

#define     _STDIO_HANDLE_COUNT         256

__attribute__ ((dllimport, ms_abi)) int32_t ZwClose (
    void *Handle
);

__attribute__ ((dllimport, ms_abi)) int32_t ZwCreateFile (
    void **FileHandle,
    uint32_t DesiredAccess,
    OBJECT_ATTRIBUTES *ObjectAttributes,
    IO_STATUS_BLOCK *IoStatusBlock,
    void *AllocationSize,
    uint32_t FileAttributes,
    uint32_t ShareAccess,
    uint32_t CreateDisposition,
    uint32_t CreateOptions,
    void *EaBuffer,
    uint32_t EaLength
);

__attribute__ ((dllimport, ms_abi)) int32_t ZwOpenFile (
    void **FileHandle,
    uint32_t DesiredAccess,
    OBJECT_ATTRIBUTES *ObjectAttributes,
    IO_STATUS_BLOCK *IoStatusBlock,
    uint32_t ShareAccess,
    uint32_t OpenOptions
);

__attribute__ ((dllimport, ms_abi)) int32_t ZwReadFile (
    void *FileHandle,
    void *Event,
    void *ApcRoutine,
    void *ApcContext,
    IO_STATUS_BLOCK *IoStatusBlock,
    void *Buffer,
    uint32_t Length,
    void *ByteOffset,
    void *Key
);

__attribute__ ((dllimport, ms_abi)) int32_t ZwWriteFile (
    void *FileHandle,
    void *Event,
    void *ApcRoutine,
    void *ApcContext,
    IO_STATUS_BLOCK *IoStatusBlock,
    const void *Buffer,
    uint32_t Length,
    void *ByteOffset,
    void *Key
);

__attribute__ ((dllimport, ms_abi)) int32_t ZwQueryInformationFile (
    void *FileHandle,
    IO_STATUS_BLOCK *IoStatusBlock,
    void *FileInformation,
    uint32_t Length,
    uint32_t FileInformationClass
);

__attribute__ ((dllimport, ms_abi)) int32_t ZwSetInformationFile (
    void *FileHandle,
    IO_STATUS_BLOCK *IoStatusBlock,
    void *FileInformation,
    uint32_t Length,
    uint32_t FileInformationClass
);

#define     _DEFAULT_BUFSIZE            1024
static FILE _iob[3] = { 0 };

__attribute ((dllexport)) FILE *__iob_func (void) {
    return _iob;
}

char _stdin_buf[_DEFAULT_BUFSIZE];
char _stdout_buf[_DEFAULT_BUFSIZE];
char _stderr_buf[_DEFAULT_BUFSIZE];

#define     G_FILE_FLAG_EOF             (1U << 26)          /* end of file reached */
#define     G_FILE_FLAG_ERROR           (1U << 27)          /* when a file error has occured */

static void *__stdio_handle_table[_STDIO_HANDLE_COUNT];
static uint8_t __stdio_handle_in_use[_STDIO_HANDLE_COUNT];

extern void __msvcrt_set_errno (int32_t status);

static int __stdio_impl_get_handle (int fd, void **handle) {
    
    if (fd < 0 || fd >= _STDIO_HANDLE_COUNT) {
    
        errno = EBADF;
        return -1;
    
    }
    
    if (!__stdio_handle_in_use[fd]) {
    
        errno = EBADF;
        return -1;
    
    }
    
    *handle = __stdio_handle_table[fd];
    return 0;

}

static int __stdio_impl_close (int __fd) {

    int32_t status;
    void *handle;
    
    if (__stdio_impl_get_handle (__fd, &handle) < 0) {
        return -1;
    }
    
    status = ZwClose (handle);
    
    if (status < 0) {
    
        __msvcrt_set_errno (status);
        return -1;
    
    }
    
    __stdio_handle_table[__fd] = NULL;
    __stdio_handle_in_use[__fd] = 0;

    return 0;

}

static int __stdio_impl_open (const char *__file, int __oflag) {

    UNICODE_STRING name;
    
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK io_status;
    
    uint16_t path[512];
    void *handle = NULL;
    
    uint32_t access, disposition;
    size_t len;
    
    int32_t status;
    int fd;

    if (!__file) {
    
        errno = EINVAL;
        return -1;
    
    }

    for (len = 0; __file[len]; len++) {

        if (len >= 511 || (unsigned char) __file[len] > 0x7F) {
        
            errno = len >= 511 ? 38 : EINVAL;
            return -1;
        
        }

        path[len] = (uint16_t) (unsigned char) __file[len];

    }

    if (!len) {
    
        errno = ENOENT;
        return -1;
    
    }

    path[len] = 0;

    name.Length = (uint16_t) (len * 2);
    name.MaximumLength = (uint16_t) ((len + 1) * 2);
    name.Buffer = path;

    attributes.Length = sizeof (attributes);
    attributes.RootDirectory = NULL;
    attributes.ObjectName = &name;
    attributes.Attributes = 0;
    attributes.SecurityDescriptor = NULL;
    attributes.SecurityQualityOfService = NULL;

    if (__oflag & O_RDWR) {
        access = GENERIC_READ | GENERIC_WRITE;
    } else if (__oflag & O_WRONLY) {
        access = GENERIC_WRITE;
    } else {
        access = GENERIC_READ;
    }
    
    if (__oflag & O_APPEND) {
    
        access &= ~GENERIC_WRITE;
        access |= FILE_APPEND_DATA;
    
    }
    
    disposition = FILE_OPEN;
    
    if (__oflag & O_CREAT) {
    
        if (__oflag & O_EXCL) {
            disposition = FILE_CREATE;
        } else if (__oflag & O_TRUNC) {
            disposition = FILE_OVERWRITE_IF;
        } else {
            disposition = FILE_OPEN_IF;
        }
    
    } else if (__oflag & O_TRUNC) {
        disposition = FILE_OVERWRITE;
    }
    
    for (fd = 0; fd < _STDIO_HANDLE_COUNT; fd++) {
    
        if (!__stdio_handle_in_use[fd]) {
            break;
        }
    
    }
    
    if (fd == _STDIO_HANDLE_COUNT) {
    
        errno = EMFILE;
        return -1;
    
    }
    
    __stdio_handle_in_use[fd] = 1;
    
    if ((status = ZwCreateFile (&handle, access | SYNCHRONIZE, &attributes, &io_status, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, disposition, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0)) >= 0) {
    
        __stdio_handle_table[fd] = handle;
        return fd;

    }

    __stdio_handle_in_use[fd] = 0;
    __msvcrt_set_errno (status);
    
    return -1;

}

static ssize_t __stdio_impl_write (const void *buf, size_t nbytes, FILE *stream) {

    IO_STATUS_BLOCK io_status;
    
    int32_t status;
    void *handle;
    
    if (__stdio_impl_get_handle (stream->_fileno, &handle) < 0) {
        return -1;
    }
    
    status = ZwWriteFile (handle, NULL, NULL, NULL, &io_status, buf, (uint32_t) (nbytes > 0x7FFFFFFFU ? 0x7FFFFFFFU : nbytes), NULL, NULL);
    
    if (status < 0) {
    
        __msvcrt_set_errno (status);
        return -1;
    
    }
    
    return (ssize_t) io_status.Information;

}

static ssize_t __stdio_impl_read (const void *buf, size_t nbytes, FILE *stream) {

    IO_STATUS_BLOCK io_status;
    
    int32_t status;
    void *handle;
    
    if (__stdio_impl_get_handle (stream->_fileno, &handle) < 0) {
        return -1;
    }
    
    status = ZwReadFile (handle, NULL, NULL, NULL, &io_status, (void *) buf, (uint32_t) (nbytes > 0x7FFFFFFFU ? 0x7FFFFFFFU : nbytes), NULL, NULL);
    
    if (status < 0) {
    
        __msvcrt_set_errno (status);
        return -1;
    
    }
    
    return (ssize_t) io_status.Information;

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

__attribute__ ((dllexport)) int fclose (FILE *stream) {

    int ret;
    
    if (!stream) {
        return EOF;
    }
    
    ret = fflush (stream);
    
    if (__stdio_impl_close (stream->_fileno) < 0) {
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

__attribute__ ((dllexport)) int feof (FILE *stream) {

    int res = 0;
    
    if (stream->_impl_eof) {
        res = stream->_impl_eof (stream);
    }
    
    return res;

}

__attribute__ ((dllexport)) int ferror (FILE *stream) {

    int res = 0;
    
    if (stream->_impl_error) {
        res = stream->_impl_error (stream);
    }
    
    return res;

}

__attribute__ ((dllexport)) int fflush (FILE *stream) {

    FILE *file;
    
    ssize_t written;
    size_t done;
    
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
        
            if (fseek (stream, -((off_t) (stream->_buffer_len - stream->_buffer_pos)), SEEK_CUR) < 0) {
            
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

__attribute__ ((dllexport)) int setvbuf (FILE *stream, char *buf, int mode, size_t size) {

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

__attribute__ ((dllexport)) FILE *fopen (const char *path, const char *mode) {

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
    
    if ((fd = __stdio_impl_open (path, flags)) < 0) {
    
        free (file);
        return NULL;
    
    }
    
    if (!__fdopen_static (fd, mode, file)) {
        return file;
    }
    
    __stdio_impl_close (fd);
    free (file);
    
    return NULL;

}

__attribute__ ((dllexport)) size_t fread (const void *ptr, size_t size, size_t nmemb, FILE *stream) {

    uint8_t *pos;
    ssize_t read;
    
    size_t total, done;
    
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
    
    stream->_flags |= G_FILE_FLAG_ERROR;
    return 0;

}

__attribute__ ((dllexport)) int fseek (FILE *stream, long offset, int whence) {

    IO_STATUS_BLOCK io_status;
    
    FILE_POSITION_INFORMATION position;
    FILE_STANDARD_INFORMATION standard;
    
    void *handle;
    
    int32_t status;
    int64_t base;
    
    if (!stream) {
    
        errno = EINVAL;
        return -1;
    
    }
    
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
    
        errno = EINVAL;
        return -1;
    
    }
    
    if (whence == SEEK_CUR && stream->_unget_pending) {
        offset--;
    }
    
    if (__stdio_impl_get_handle (stream->_fileno, &handle) < 0 || fflush (stream) == EOF) {
        return -1;
    }
    
    base = 0;
    
    if (whence == SEEK_CUR) {
    
        status = ZwQueryInformationFile (handle, &io_status, &position, sizeof (position), FilePositionInformation);
        
        if (status < 0) {
        
            __msvcrt_set_errno (status);
            return -1;
        
        }
        
        base = position.CurrentByteOffset;
    
    } else if (whence == SEEK_END) {
    
        status = ZwQueryInformationFile (handle, &io_status, &standard, sizeof (standard), FileStandardInformation);
        
        if (status < 0) {
        
            __msvcrt_set_errno (status);
            return -1;
        
        }
        
        base = standard.EndOfFile;
    
    }
    
    if (offset > 0 && base > 0x7FFFFFFFFFFFFFFFLL - (int64_t) offset) {
    
        errno = ERANGE;
        return -1;
    
    }
    
    position.CurrentByteOffset = base + (int64_t) offset;
    
    if (position.CurrentByteOffset < 0) {
    
        errno = EINVAL;
        return -1;
    
    }
    
    status = ZwSetInformationFile (handle, &io_status, &position, sizeof (position), FilePositionInformation);
    
    if (status < 0) {
    
        __msvcrt_set_errno (status);
        return -1;
    
    }
    
    stream->_flags &= ~G_FILE_FLAG_EOF;
    stream->_unget_pending = 0;
    
    return 0;

}

__attribute__ ((dllexport)) long ftell (FILE *stream) {

    long pos;
    
    if (!stream) {
    
        errno = EINVAL;
        return -1L;
    
    }
    
    pos = (long) fseek (stream, 0, SEEK_CUR);
    
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

__attribute__ ((dllexport)) size_t fwrite (const void *ptr, size_t size, size_t nmemb, FILE *stream) {

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
        
        if (stream->_unget_pending) {
        
            ((uint8_t *) ptr)[done++] = (uint8_t) stream->_unget_character;
            stream->_unget_pending = 0;
        
        }
        
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
            
            if (stream->_buffer_pos == stream->_buffer_size ||
                (stream->_mode == _IOLBF && ((const char *) ptr)[done - 1] == '\n')) {
            
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


__attribute__ ((packed)) void clearerr (FILE *stream) {

    if (stream) {
        stream->_flags &= ~(G_FILE_FLAG_EOF | G_FILE_FLAG_ERROR);
    }

}

__attribute ((dllexport)) int fgetc (FILE *stream) {

    unsigned char ch = 0;
    
    if (fread (&ch, 1, 1, stream) != 1) {
        return EOF;
    }
    
    return ch;

}

__attribute__ ((dllexport)) int fileno (FILE *stream) {

    if (!stream) {
    
        errno = EBADF;
        return -1;
    
    }
    
    return stream->_fileno;

}

__attribute__ ((dllexport)) int fputc (int ch, FILE *stream) {

    unsigned char c = (unsigned char) ch;
    
    if (fwrite (&c, 1, 1, stream) != 1) {
        return EOF;
    }
    
    return c;

}

__attribute__ ((dllexport)) int fputs (const char *s, FILE *stream) {

    size_t length = strlen (s);
    
    if (fwrite (s, 1, length, stream) != length) {
        return EOF;
    }
    
    return 0;

}

__attribute__ ((dllexport)) int getc (FILE *stream) {
    return fgetc (stream);
}

__attribute__ ((dllexport)) int getchar (void) {
    return fgetc (stdin);
}

__attribute__ ((dllexport)) int putchar (int ch) {
    return fputc (ch, stdout);
}

__attribute__ ((dllexport)) void rewind (FILE *stream) {

    if (!stream) {
        return;
    }
    
    fseek (stream, 0L, SEEK_SET);
    stream->_flags &= ~(G_FILE_FLAG_EOF | G_FILE_FLAG_ERROR);

}

__attribute__ ((dllexport)) int ungetc (int ch, FILE *stream) {

    if (!stream || ch == EOF || stream->_unget_pending) {
        return EOF;
    }
    
    stream->_unget_character = (unsigned char) ch;
    stream->_unget_pending = 1;
    stream->_flags &= ~G_FILE_FLAG_EOF;
    
    return (unsigned char) ch;

}

__attribute__ ((dllexport)) void __init_stdio (void) {

    __open_file_list_initialize ();
    memset (stdin, 0, sizeof (FILE));
    
    __stdio_handle_table[STDIN_FILENO] = (void *) (uintptr_t) STDIN_FILENO;
    __stdio_handle_in_use[STDIN_FILENO] = 1;
    
    __fdopen_static (STDIN_FILENO, "r", stdin);
//    setvbuf (stdin, _stdin_buf, _IOLBF, _DEFAULT_BUFSIZE);
    
    memset (stdout, 0, sizeof (FILE));
    
    __stdio_handle_table[STDOUT_FILENO] = (void *) (uintptr_t) STDOUT_FILENO;
    __stdio_handle_in_use[STDOUT_FILENO] = 1;
    
    __fdopen_static (STDOUT_FILENO, "w", stdout);
//    setvbuf (stdout, _stdout_buf, _IOLBF, _DEFAULT_BUFSIZE);
    
    memset (stderr, 0, sizeof (FILE));
    
    __stdio_handle_table[STDERR_FILENO] = (void *) (uintptr_t) STDERR_FILENO;
    __stdio_handle_in_use[STDERR_FILENO] = 1;
    
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

__attribute__ ((dllexport)) int vfprintf (FILE *stream, const char *fmt, va_list ap) {

    struct callback callback = { 0 };
    
    char buffer[PRINTF_BUFFER_SIZE];
    int ret;
    
    callback.stream = stream;
    callback.buffer = buffer;
    
    ret = xvasprintf (cb_printf, &callback, fmt, ap);
    flush_printf (&callback);
    
    return callback.error ? EOF : ret;

}

__attribute__ ((dllexport)) int fprintf (FILE *stream, const char *fmt, ...) {

    va_list ap;
    int ret;
    
    va_start (ap, fmt);
    ret = vfprintf (stream, fmt, ap);
    va_end (ap);
    
    return ret;

}

__attribute__ ((dllexport)) int printf (const char *fmt, ...) {

    va_list ap;
    int ret;
    
    va_start (ap, fmt);
    ret = vfprintf (stdout, fmt, ap);
    va_end (ap);
    
    return ret;

}

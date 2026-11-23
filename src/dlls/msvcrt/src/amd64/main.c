/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <errno.h>
#include    <stdint.h>

int errno = 0;

__attribute__ ((dllexport)) int *_errno (void) {
    return &errno;
}

void __msvcrt_set_errno (int32_t status) {

    switch (status & 0x7FFFFFFF) {
    
        case (0xC0000034U & 0x7FFFFFFF):                    /* STATUS_OBJECT_NAME_NOT_FOUND */
        
            errno = ENOENT;
            break;
        
        case (0xC0000008U & 0x7FFFFFFF):                    /* STATUS_INVALID_HANDLE */
        
            errno = EBADF;
            break;
        
        case (0xC00000A3U & 0x7FFFFFFF):                    /* STATUS_DEVICE_NOT_READY */
        
            errno = EAGAIN;
            break;
        
        case (0xC0000017U & 0x7FFFFFFF):                    /* STATUS_NO_MEMORY */
        
            errno = ENOMEM;
            break;
        
        case (0xC0000022U & 0x7FFFFFFF):                    /* STATUS_ACCESS_DENIED */
        
            errno = EACCES;
            break;
        
        case (0xC0000005U & 0x7FFFFFFF):                    /* STATUS_ACCESS_VIOLATION */
        
            errno = EFAULT;
            break;
        
        case (0x80000011U & 0x7FFFFFFF):                    /* STATUS_DEVICE_BUSY */
        
            errno = EBUSY;
            break;
        
        case (0xC0000035U & 0x7FFFFFFF):                    /* STATUS_OBJECT_NAME_COLLISION */
        
            errno = EEXIST;
            break;
        
        case (0xC000000EU & 0x7FFFFFFF):                    /* STATUS_NO_SUCH_DEVICE */
        
            errno = ENODEV;
            break;
        
        case (0xC0000103U & 0x7FFFFFFF):                    /* STATUS_NOT_A_DIRECTORY */
        
            errno = ENOTDIR;
            break;
        
        case (0xC00000BAU & 0x7FFFFFFF):                    /* STATUS_FILE_IS_A_DIRECTORY */
        
            errno = EISDIR;
            break;
        
        case (0xC000000DU & 0x7FFFFFFF):                    /* STATUS_INVALID_PARAMETER */
        case (0xC0000033U & 0x7FFFFFFF):                    /* STATUS_OBJECT_NAME_INVALID */
        
            errno = EINVAL;
            break;
        
        case (0xC000011FU & 0x7FFFFFFF):                    /* STATUS_TOO_MANY_OPENED_FILES */
        
            errno = EMFILE;
            break;
        
        case (0xC000007FU & 0x7FFFFFFF):                    /* STATUS_DISK_FULL */
        
            errno = ENOSPC;
            break;
        
        case (0xC00000A2U & 0x7FFFFFFF):                    /* STATUS_MEDIA_WRITE_PROTECTED */
        
            errno = EROFS;
            break;
        
        case (0xC0000106U & 0x7FFFFFFF):                    /* STATUS_NAME_TOO_LONG */
        
            errno = 38;                                     /* MSVCRT ENAMETOOLONG */
            break;
        
        case (0xC0000002U & 0x7FFFFFFF):                    /* STATUS_NOT_IMPLEMENTED */
        case (0xC00000BBU & 0x7FFFFFFF):                    /* STATUS_NOT_SUPPORTED */
        
            errno = 40;                                     /* MSVCRT ENOSYS */
            break;
        
        case (0xC0000095U & 0x7FFFFFFF):                    /* STATUS_INTEGER_OVERFLOW */
        
            errno = ERANGE;
            break;
        
        default:
        
            errno = EIO;
            break;
    
    }

}

static int _commode = 0;

__attribute__ ((dllexport)) int *__p__commode (void) {
    return &_commode;
}

extern char **__initenv;

__attribute__ ((dllexport)) char *** __p___initenv (void) {
    return &__initenv;
}

__attribute__ ((dllexport)) int _XcptFilter (unsigned long xcptnum, struct _EXCEPTION_POINTERS *pxcptinfoptrs) {

    (void) xcptnum;
    (void) pxcptinfoptrs;

    return 0;

}

typedef void (*_PVFV) (void);

__attribute__ ((dllexport)) void _initterm (_PVFV *first, _PVFV *last) {

    while (first < last) {
    
        if (*first) {
            (*first) ();
        }
        
        first++;
    
    }

}

int _fmode = 0x4000;

__attribute__ ((dllexport)) int *__p__fmode (void) {
    return &_fmode;
}

static int app_type = 0;

__attribute__ ((dllexport)) void __set_app_type (int type) {
    app_type = type;
}

__attribute__ ((dllexport)) int _except_handler3 (struct _EXCEPTION_RECORD *exception_record, struct _EXCEPTION_REGISTRATION_RECORD *registration, struct _CONTEXT *context, void *dispatcher) {

    (void) exception_record;
    (void) registration;
    (void) context;
    (void) dispatcher;

    return 1;

}

extern void __init_stdio (void);

int DllMain (void *instance, unsigned long reason, void *reserved) {

    (void) instance;
    (void) reserved;

    switch (reason) {

        case 1:                         /* DLL_PROCESS_ATTACH */
        
            __init_stdio ();
            break;
        
        default:
        
            break;

    }

    return 1;

}

/******************************************************************************
 * @file            vsnprintf.c
 *****************************************************************************/
#include    <stdarg.h>
#include    <stddef.h>
#include    <stdint.h>

struct callback {

    char *buffer;
    int index, length;

};

int xvasprintf (int (*callback) (struct callback *, char), void *user_data, const char *fmt, va_list ap);

static int cb_printf (struct callback *callback, char ch) {

    int ret = 0;
    
    if (ch == '\t') {
    
        int count = 4 - (callback->index % 4);
        
        while (count--) {
        
            if (callback->index < callback->length - 1) {
                callback->buffer[callback->index++] = ch;
            }
            
            ret++;
        
        }
        
        return ret;
    
    }
    
    if (callback->index < callback->length - 1) {
        callback->buffer[callback->index++] = ch;
    }
    
    return 1;

}

int vsnprintf (char *buf, size_t size, const char *fmt, va_list ap) {

    struct callback callback = { 0 };
    int ret;
    
    callback.buffer = buf;
    callback.length = size;
    
    ret = xvasprintf (cb_printf, &callback, fmt, ap);
    buf[callback.index] = '\0';
    
    return ret;

}

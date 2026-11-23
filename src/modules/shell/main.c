/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <fcntl.h>
#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>

#define     IPC_MESSAGE_ANY                                 (1U << 0)
#define     IPC_MESSAGE_LOAD                                (1U << 1)

typedef struct {

    void *data;
    
    size_t size;
    uintptr_t address;

} ipc_buffer_t;

#define     IPC_WRONGTYPE                                   4
#define     IPC_REDIRECT                                    6

typedef struct {

    uint32_t source, type;
    
    uintptr_t *data;
    size_t data_capacity, data_count;
    
    ipc_buffer_t *buffers;
    size_t buffers_capacity, buffers_count;
    
    void *buffer;
    size_t buffer_size, size;

} ipc_message_t;

static int ipc_receive (uint32_t source, ipc_message_t *message) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x1001), "D" (source), "S" (message) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x1001), "b" (source), "c" (message) : "memory");
#endif
    
    return ret;

}

static int ipc_send (uint32_t destination, const ipc_message_t *message) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x1000), "D" (destination), "S" (message) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x1000), "b" (destination), "c" (message) : "memory");
#endif
    
    return ret;

}

static size_t read_interpreter (int fd, char *file) {

    size_t len = 0, i, start, end, size;
    int count;
    
    while (len < 4096) {
    
        if ((count = read (fd, file + len, 4096 - len)) < 0) {
            return 0;
        }
        
        if (!count) {
            break;
        }
        
        if (len + (size_t) count >= 2 && (file[0] != '#' || file[1] != '!')) {
            return 0;
        }
        
        for (i = len; i < len + (size_t) count; i++) {
        
            if (!file[i]) {
                return 0;
            }
            
            if (file[i] == '\n') {
            
                len = i;
                goto line;
            
            }
        
        }
        
        len += (size_t) count;
    
    }
    
line:
    
    if (len < 3 || len == 4096) {
        return 0;
    }
    
    while (len > 2 && (file[len - 1] == ' ' || file[len - 1] == '\t')) {
        len--;
    }
    
    for (start = 2; start < len && (file[start] == ' ' || file[start] == '\t'); start++) {
        ;
    }
    
    if (start == len || file[start] != '/') {
        return 0;
    }
    
    for (end = start; end < len && file[end] != ' ' && file[end] != '\t'; end++) {
        ;
    }
    
    size = 0;
    
    for (i = start; i < end; i++) {
        file[size++] = file[i];
    }
    
    file[size++] = 0;
    
    while (end < len && (file[end] == ' ' || file[end] == '\t')) {
        end++;
    }
    
    if (end < len) {
    
        for (i = end; i < len; i++) {
            file[size++] = file[i];
        }
        
        file[size++] = 0;
    
    }
    
    return size;

}

int main (int argc, char **argv) {

    ipc_message_t message = { 0 };
    
    char *path, *file;
    int fd;
    
    (void) argc;
    (void) argv;
    
    if (!(path = malloc (4096))) {
        return -1;
    }
    
    if (!(file = malloc (4096))) {
    
        free (path);
        return -1;
    
    }
    
    if (!(message.data = malloc (sizeof (*message.data)))) {
    
        free (path);
        free (file);
        
        return -1;
    
    }
    
    message.data_capacity = 1;
    
    for (;;) {
    
        message.buffers_count = 0;
        message.data_count = 0;
        
        memset (path, 0, 4096);
        
        message.buffer = path;
        message.buffer_size = 4096;
        
        message.type = IPC_MESSAGE_LOAD;
        
        if (ipc_receive (IPC_MESSAGE_ANY, &message) != 0) {
            continue;
        }
        
        if (message.type != IPC_MESSAGE_LOAD) {
            continue;
        }
        
        message.data[0] = IPC_WRONGTYPE;
        
        message.data_count = 1;
        message.size = 0;
        
        if ((fd = open (path, O_RDONLY)) >= 0) {
        
            message.size = read_interpreter (fd, file);
            close (fd);
            
            if (message.size) {
            
                message.data[0] = IPC_REDIRECT;
                message.buffer = file;
            
            }
        
        }
        
        ipc_send (message.source, &message);
        
    }

}

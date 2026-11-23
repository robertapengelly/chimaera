/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <stdint.h>
#include    <stdio.h>
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

#define     IPC_MESSAGE_EXEC_CHIMAERA                       (1U << 2)
#define     IPC_MESSAGE_EXEC_MODULE                         (1U << 4)

extern void armv7_start (void *);

static int armv7_executable (const char *path) {

    unsigned char header[52];
    size_t size;
    
    FILE *fp;
    
    if (!(fp = fopen (path, "rb"))) {
        return 0;
    }
    
    size = fread (header, 1, sizeof (header), fp);
    fclose (fp);
    
    if (size >= 52 && header[0] == 0x7F && header[1] == 'E' && header[2] == 'L' && header[3] == 'F') {
        return header[4] == 1 && header[5] == 1 && header[6] == 1 && header[16] == 2 && !header[17] && header[18] == 40 && !header[19];
    }
    
    if (size >= 28 && header[0] == 0xCE && header[1] == 0xFA && header[2] == 0xED && header[3] == 0xFE) {
        return header[4] == 12 && !header[5] && !header[6] && !header[7] && header[12] == 2 && !header[13] && !header[14] && !header[15];
    }
    
    return 0;

}

int main (int argc, char **argv) {

    ipc_message_t message = { 0 };
    
    char *path;
    uintptr_t data;
    
    (void) argc;
    (void) argv;
    
    if (!(path = malloc (4096))) {
        return -1;
    }
    
    message.data = &data;
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
        
        message.data_count = 1;
        message.size = 0;
        
        if (armv7_executable (path)) {
        
            data = (uintptr_t) armv7_start;
            
            message.type = IPC_MESSAGE_EXEC_CHIMAERA | IPC_MESSAGE_EXEC_MODULE;
            ipc_send (IPC_MESSAGE_ANY, &message);
            
            continue;
        
        }
        
        data = IPC_WRONGTYPE;
        ipc_send (message.source, &message);
        
    }

}

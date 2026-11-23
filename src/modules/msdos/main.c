/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>

#include    "loader.h"

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

extern void msdos_start (void *);

static int msdos_executable (const char *path) {

    unsigned char signature[4], offset[4];
    
    uint16_t paragraphs, max_paragraphs;
    int is_mz, mz;
    
    FILE *fp;
    
    const char *p;
    uint32_t header;
    
    if (!(fp = fopen (path, "rb"))) {
        return 0;
    }
    
    mz = fread (signature, 1, 2, fp) == 2 && signature[0] == 'M' && signature[1] == 'Z';
    
    if (mz && fseek (fp, 0x3C, SEEK_SET) == 0 && fread (offset, 1, sizeof (offset), fp) == sizeof (offset)) {
    
        header = (uint32_t) offset[0] | ((uint32_t) offset[1] << 8) | ((uint32_t) offset[2] << 16) | ((uint32_t) offset[3] << 24);
        
        if (header >= 0x40 && header <= 0x7FFFFFFFUL && fseek (fp, (long) header, SEEK_SET) == 0 && fread (signature, 1, sizeof (signature), fp) == sizeof (signature)) {
        
            if ((signature[0] == 'P' && signature[1] == 'E' && !signature[2] && !signature[3]) || (signature[0] == 'N' && signature[1] == 'E') || (signature[0] == 'L' && (signature[1] == 'E' || signature[1] == 'X'))) {
            
                fclose (fp);
                return 0;
            
            }
        
        }
    
    }
    
    fclose (fp);
    
    if (!mz && (!(p = strrchr (path, '.')) || strlen (p) != 4 || (p[1] != 'C' && p[1] != 'c') || (p[2] != 'O' && p[2] != 'o') || (p[3] != 'M' && p[3] != 'm'))) {
        return 0;
    }
    
    return dos_executable_memory (path, &paragraphs, &max_paragraphs, &is_mz) && mz == is_mz;

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
        
        if (msdos_executable (path)) {
        
            data = (uintptr_t) msdos_start;
            
            message.type = IPC_MESSAGE_EXEC_CHIMAERA | IPC_MESSAGE_EXEC_MODULE;
            ipc_send (IPC_MESSAGE_ANY, &message);
            
            continue;
        
        }
        
        data = IPC_WRONGTYPE;
        ipc_send (message.source, &message);
        
    }

}

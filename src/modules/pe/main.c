/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>

#define     IPC_MESSAGE_ANY                                 (1U << 0)
#define     IPC_MESSAGE_LOAD                                (1U << 1)

typedef struct {

    void *data;
    
    size_t size;
    uintptr_t address;

} ipc_buffer_t;

typedef struct {

    uint32_t source, type;
    
    uintptr_t *data;
    size_t data_capacity, data_count;
    
    ipc_buffer_t *buffers;
    size_t buffers_capacity, buffers_count;
    
    void *buffer;
    size_t buffer_size, size;

} ipc_message_t;

typedef enum {

    IPC_SUCCESSFUL = 0,
    IPC_NOTENOUGHMEMORY,
    IPC_NOTFOUND,
    IPC_ACCESSDENIED,
    IPC_WRONGTYPE,
    IPC_NULLPOINTER

} IPC_ERROR;

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

int ipc_message_add_data (ipc_message_t *message, uintptr_t value) {

    if (message->data_count == message->data_capacity) {
    
        uintptr_t *data;
        size_t capacity;
        
        if (message->data_capacity > ((size_t) -1 / sizeof (*data)) / 2) {
            return -1;
        }
        
        capacity = message->data_capacity ? message->data_capacity * 2 : 4;
        
        if (!(data = realloc (message->data, capacity * sizeof (*data)))) {
            return -1;
        }
        
        message->data = data;
        message->data_capacity = capacity;
    
    }
    
    message->data[message->data_count++] = value;
    return 0;

}

int ipc_message_add_buffer (ipc_message_t *message, void *data, size_t size, uintptr_t address) {

    if (message->buffers_count == message->buffers_capacity) {
    
        ipc_buffer_t *buffers;
        size_t capacity;
        
        if (message->buffers_capacity > ((size_t) -1 / sizeof (*buffers)) / 2) {
            return -1;
        }
        
        capacity = message->buffers_capacity ? message->buffers_capacity * 2 : 4;
        
        if (!(buffers = realloc (message->buffers, capacity * sizeof (*buffers)))) {
            return -1;
        }
        
        message->buffers = buffers;
        message->buffers_capacity = capacity;
    
    }
    
    message->buffers[message->buffers_count].data = data;
    message->buffers[message->buffers_count].size = size;
    message->buffers[message->buffers_count].address = address;
    
    message->buffers_count++;
    return 0;

}

struct image_allocation { struct image_allocation *next; };
static struct image_allocation *image_allocations = 0;

void *pe_allocate_image (size_t size) {

    struct image_allocation *allocation;
    
    if (!size || size > (size_t) -1 - 4095 - sizeof (*allocation)) {
        return 0;
    }
    
    if (!(allocation = malloc (size + 4095 + sizeof (*allocation)))) {
        return 0;
    }
    
    allocation->next = image_allocations;
    image_allocations = allocation;
    
    return (void *) (((uintptr_t) (allocation + 1) + 4095) & ~((uintptr_t) 4095));

}

static void pe_free_images (void) {

    struct image_allocation *allocation;
    
    while (image_allocations) {
        
        allocation = image_allocations;
        image_allocations = allocation->next;
        
        free (allocation);
        
    }

}

extern int load (const char *file, ipc_message_t *message);

int main (int argc, char **argv) {

    ipc_message_t message = { 0 };
    char *path;
    
    (void) argc;
    (void) argv;
    
    if (!(path = malloc (4096))) {
        return -1;
    }
    
    if (!(message.data = malloc (sizeof (*message.data)))) {
    
        free (path);
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
        
        if (load (message.buffer, &message)) {
        
            pe_free_images ();
            
            message.data[0] = IPC_WRONGTYPE;
            message.data_count = 1;
            
            message.buffers_count = 0;
            message.buffer = 0;
            message.size = 0;
            
            message.type = IPC_MESSAGE_LOAD;
            ipc_send (message.source, &message);
            
            continue;
        
        }
        
        message.buffer = 0;
        message.size = 0;
        
        if (!ipc_send (IPC_MESSAGE_ANY, &message)) {
            //
        }
        
        pe_free_images ();
        
    }

}

/******************************************************************************
 * @file            ipc.h
 *****************************************************************************/
#ifndef     _IPC_H
#define     _IPC_H

#include    <corazon/utils/list.h>

#include    <stddef.h>
#include    <stdint.h>

/* Messages are delivered to a process endpoint, not broadcast as events. */
#define     IPC_MESSAGE_ANY                                 (1U << 0)
#define     IPC_MESSAGE_LOAD                                (1U << 1)
#define     IPC_MESSAGE_EXEC_CHIMAERA                       (1U << 2)
#define     IPC_MESSAGE_EXEC_NT                             (1U << 3)
#define     IPC_MESSAGE_EXEC_MODULE                         (1U << 4)
#define     IPC_MESSAGE_EXEC_32BIT                          (1U << 15)

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
    IPC_NULLPOINTER,
    IPC_REDIRECT

} IPC_ERROR;

typedef enum {

    IPC_NONE = 0,
    IPC_READ = 1,
    IPC_WRITE = 2,
    IPC_DELEGATERIGHTS = 4

} IPC_RIGHTS;

typedef enum {

    IPC_UINT,
    IPC_INT,
    IPC_QWORD,
    IPC_SIZE,
    IPC_FLOAT,
    IPC_STRING,
    IPC_FILE,
    IPC_FOLDER

} IPC_TYPE;

typedef struct {

    uint32_t   owner;
    IPC_RIGHTS privileges;

} ipc_certificate_t;

typedef struct ipc_node {

    char *name;
    IPC_TYPE type;
    
    union {
    
        int64_t integer;                /* IPC_INT */
        uint64_t uinteger;              /* IPC_QWORD, IPC_SIZE and IPC_UINT */
        
        double floatnum;
        char *string;
        
        void *file;
        list_t folder;
    
    } data;
    
    uint32_t owner;                     /* Owner has always full access */
    
    IPC_RIGHTS general;                 /* General access rights */
    list_t *access_table;               /* list of ipc_certificate_t*. Content overrides general ipc certificate */
    
    struct ipc_node *parent;            /* Points to the parent item in the tree */

} ipc_node_t;

ipc_node_t *ipc_get_node (const char* path);

IPC_ERROR ipc_create_node (const char* path, ipc_node_t **node, IPC_TYPE type);
IPC_ERROR ipc_set_int (const char* path, const int64_t* source, IPC_TYPE type);
IPC_ERROR ipc_set_task_working_node (const char* path, int create);

IPC_ERROR ipc_delete_key (const char* path);
IPC_ERROR ipc_delete_node (ipc_node_t* node);

#endif      /* _IPC_H */

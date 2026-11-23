/******************************************************************************
 * @file            load.c
 *****************************************************************************/
#include    <fcntl.h>
#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>

#include    <axe.h>

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

#define     IPC_MESSAGE_EXEC_CHIMAERA                       (1U << 2)

extern int ipc_message_add_buffer (ipc_message_t *message, void *data, size_t size, uintptr_t address);
extern int ipc_message_add_data (ipc_message_t *message, uintptr_t value);

static uint64_t array_to_int (uint8_t *arr, int size) {

    uint64_t val = 0;
    int i;
    
    for (i = 0; i < size; i++) {
        val |= (uint64_t) arr[i] << (8 * i);
    }
    
    return val;

}

static void int_to_array (uint8_t *arr, uint64_t val, int size) {

    int i;
    
    for (i = 0; i < size; i++) {
        arr[i] = (val >> (i * 8)) & 0xff;
    }

}

int load (const char *file, ipc_message_t *message) {

    struct axe_exec32 exec;
    uintptr_t addr = 0;
    
    struct axe_relocation_info32 *reloc;
    size_t file_size = 0;
    
    uint64_t symbolnum, address;
    size_t relocs = 0, bss_size, i;
    
    int fd;
    
    if ((fd = open (file, O_RDONLY)) < 0) {
        return 1;
    }
    
    if (read (fd, (uint8_t *) &exec, 4) < 4) {
    
        close (fd);
        return 1;
    
    }
    
    if (exec.a_info[0] != 'A' || exec.a_info[1] != 'X' || exec.a_info[2] != 'E') {
    
        close (fd);
        return 1;
    
    }
    
    if (exec.a_info[3] != 3) {
    
        close (fd);
        return 1;
    
    }
    
    if (read (fd, (uint8_t *) &exec + 4, sizeof (exec) - 4) != (ssize_t) (sizeof (exec) - 4)) {
    
        close (fd);
        return 1;
    
    }
    
    file_size = array_to_int (exec.a_text, 4) + array_to_int (exec.a_data, 4) + array_to_int (exec.a_bss, 4);
    
    if (!(addr = malloc (file_size))) {
    
        close (fd);
        return 1;
    
    }
    
    file_size -= array_to_int (exec.a_bss, 4);
    
    if (read (fd, (void *) addr, file_size) != (ssize_t) file_size) {
    
        close (fd);
        return 1;
    
    }
    
    if ((bss_size = array_to_int (exec.a_bss, 4))) {
        memset ((uint8_t *) addr + file_size, 0, bss_size);
    }
    
    if ((relocs = array_to_int (exec.a_trsize, 4) / sizeof (*reloc))) {
    
        void *temp;
        
        uint64_t value;
        ssize_t size;
        
        size = array_to_int (exec.a_trsize, 4);
        
        if (!(temp = malloc (size))) {
        
            close (fd);
            return 1;
        
        }
        
        if (read (fd, temp, size) < size) {
        
            free (temp);
            
            close (fd);
            return 1;
        
        }
        
        reloc = (struct axe_relocation_info32 *) temp;
        
        for (i = 0; i < relocs; i++) {
        
            address = array_to_int (reloc[i].r_address, sizeof (reloc[i].r_address));
            symbolnum = array_to_int (reloc[i].r_symbolnum, sizeof (reloc[i].r_symbolnum));
            
            size = (symbolnum >> 24) & 0x06;
            
            if (size == 0x06) {
            
                value = array_to_int ((uint8_t *) addr + address, 4);
                int_to_array ((uint8_t *) addr + address, value + addr, 4);
            
            } else if (size == 0x04) {
            
                value = array_to_int ((uint8_t *) addr + address, 4);
                int_to_array ((uint8_t *) addr + address, value + addr, 4);
            
            } else if (size == 0x02) {
            
                value = array_to_int ((uint8_t *) addr + address, 2);
                int_to_array ((uint8_t *) addr + address, value + addr, 2);
            
            }
        
        }
        
        free (temp);
    
    }
    
    if ((relocs = array_to_int (exec.a_drsize, 4) / sizeof (*reloc))) {
    
        uintptr_t data = addr + array_to_int (exec.a_text, 4);
        void *temp;
        
        uint64_t value;
        ssize_t size;
        
        size = array_to_int (exec.a_drsize, 4);
        
        if (!(temp = malloc (size))) {
        
            close (fd);
            return 1;
        
        }
        
        if (read (fd, temp, size) < size) {
        
            free (temp);
            
            close (fd);
            return 1;
        
        }
        
        reloc = (struct axe_relocation_info32 *) temp;
        
        for (i = 0; i < relocs; i++) {
        
            address = array_to_int (reloc[i].r_address, sizeof (reloc[i].r_address));
            symbolnum = array_to_int (reloc[i].r_symbolnum, sizeof (reloc[i].r_symbolnum));
            
            size = (symbolnum >> 24) & 0x06;
            
            if (size == 0x06) {
            
                value = array_to_int ((uint8_t *) data + address, 4);
                int_to_array ((uint8_t *) data + address, value + addr, 4);
            
            } else if (size == 0x04) {
            
                value = array_to_int ((uint8_t *) data + address, 4);
                int_to_array ((uint8_t *) data + address, value + addr, 4);
            
            } else if (size == 0x02) {
            
                value = array_to_int ((uint8_t *) data + address, 2);
                int_to_array ((uint8_t *) data + address, value + addr, 2);
            
            }
        
        }
        
        free (temp);
    
    }
    
    close (fd);
    
    if (ipc_message_add_buffer (message, (void *) addr, file_size + bss_size, (uintptr_t) addr) || ipc_message_add_data (message, (uintptr_t) addr + array_to_int (exec.a_entry, 4))) {
    
        free ((void *) addr);
        return 1;
    
    }
    
    message->type = IPC_MESSAGE_EXEC_CHIMAERA;
    return 0;

}

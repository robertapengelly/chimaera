/******************************************************************************
 * @file            load.c
 *****************************************************************************/
#include    <ctype.h>
#include    <fcntl.h>
#include    <stddef.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>

#include    <pe.h>

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

#define     IPC_MESSAGE_EXEC_NT                             (1U << 3)
#define     IPC_MESSAGE_EXEC_32BIT                          (1U << 15)

extern int ipc_message_add_buffer (ipc_message_t *message, void *data, size_t size, uintptr_t address);
extern int ipc_message_add_data (ipc_message_t *message, uintptr_t value);

extern void *pe_allocate_image (size_t size);

static uint64_t array_to_int (uint8_t *arr, int size) {

    uint64_t val = 0;
    int i;
    
    for (i = 0; i < size; i++) {
        val |= (uint64_t) arr[i] << (8 * i);
    }
    
    return val;

}

#define     SYSTEM32            "/nt/System32"
#define     SYSWOW64            "/nt/SysWOW64"

static int load_dll (uintptr_t exe_start, struct pe_import_directory_table *import_desc, ipc_message_t *message) {

    struct msdos_header exec;
    
    void *dll_start;
    int fd;
    
    uint16_t header_size, lfanew;
    char *name1 = 0, *name2 = 0;
    
    if (!(name1 = exe_start + array_to_int (import_desc->NameRVA, 4))) {
        return 1;
    }
    
    if ((fd = open (name1, O_RDONLY)) < 0) {
    
        size_t len, i;
        
        if (!(name2 = malloc (strlen (SYSWOW64) + 1 + strlen (name1) + 1))) {
            return 1;
        }
        
        len = sprintf (name2, "%s/", SYSWOW64);
        
        for (i = 0; i < strlen (name1); i++) {
            name2[len + i] = tolower ((int) name1[i]);
        }
        
        name2[len + i] = '\0';
        
        if ((fd = open (name2, O_RDONLY)) < 0) {
        
            printf ("failed to open %s for reading\n", name1);
            
            free (name2);
            return 1;
        
        }
        
        free (name2);
    
    }
    
    if (read (fd, &exec, 2) < 2) {
    
        close (fd);
        return 1;
    
    }
    
    if (exec.e_magic[0] != 'M' || exec.e_magic[1] != 'Z') {
    
        close (fd);
        return 1;
    
    }
    
    if (read (fd, (uint8_t *) &exec + 2, 8) < 8) {
    
        close (fd);
        return 1;
    
    }
    
    header_size = array_to_int (exec.e_cparhdr, 2);
    
    if (!header_size || header_size * 16 != sizeof (exec)) {
    
        close (fd);
        return 1;
    
    }
    
    if (read (fd, (uint8_t *) &exec + 10, sizeof (exec) - 10) < (ssize_t) (sizeof (exec) - 10)) {
    
        close (fd);
        return 1;
    
    }
    
    if (header_size >= 4 && (lfanew =  array_to_int (exec.e_lfanew, 2)) != 0) {
    
        struct pe_section_table_entry *section_table;
        struct pe_optional_header *optional_hdr;
        struct pe_header pe_hdr;
        
        if (lseek (fd, lfanew, SEEK_SET) != lfanew) {
        
            close (fd);
            return 1;
        
        }
        
        if (read (fd, &pe_hdr, 4) < 4) {
        
            close (fd);
            return 1;
        
        }
        
        if (pe_hdr.Signature[0] != 'P' || pe_hdr.Signature[1] != 'E' || pe_hdr.Signature[2] != '\0' || pe_hdr.Signature[3] != '\0') {
        
            close (fd);
            return 1;
        
        }
        
        if (read (fd, (uint8_t *) &pe_hdr + 4, sizeof (pe_hdr) - 4) < (ssize_t) (sizeof (pe_hdr) - 4)) {
        
            close (fd);
            return 1;
        
        }
        
        if (pe_hdr.Machine[0] == 0x4C && pe_hdr.Machine[1] == 0x01) {
        
            uint16_t Characteristics = array_to_int (pe_hdr.Characteristics, 2);
            
            if (!(Characteristics & IMAGE_FILE_RELOCS_STRIPPED)) {
            
                uint16_t SizeOfOptionalHeader = array_to_int (pe_hdr.SizeOfOptionalHeader, 2);
                
                if (!(optional_hdr = malloc (SizeOfOptionalHeader))) {
                
                    close (fd);
                    return 1;
                
                }
                
                if (read (fd, optional_hdr, SizeOfOptionalHeader) == SizeOfOptionalHeader) {
                
                    uint16_t Magic = array_to_int (optional_hdr->Magic, 2);
                    
                    if (Magic == IMAGE_FILE_MAGIC_I386) {
                    
                        uint16_t NumberOfSections = array_to_int (pe_hdr.NumberOfSections, 2);
                        
                        if (!(section_table = malloc (sizeof (*section_table) * NumberOfSections))) {
                        
                            free (optional_hdr);
                            
                            close (fd);
                            return 1;
                        
                        }
                        
                        if (read (fd, section_table, sizeof (*section_table) * NumberOfSections) == (ssize_t) (sizeof (*section_table) * NumberOfSections)) {
                        
                            uint32_t SizeOfImage = array_to_int (optional_hdr->SizeOfImage, 4);
                            
                            if ((dll_start = pe_allocate_image (SizeOfImage))) {
                            
                                struct pe_section_table_entry *section;
                                
                                uint32_t VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData;
                                uint16_t i;
                                
                                uint32_t ImageBase, NumberOfRvaAndSizes;
                                
                                for (i = 0; i < NumberOfSections; i++) {
                                
                                    section = &section_table[i];
                                    
                                    VirtualSize = array_to_int (section->VirtualSize, 4);
                                    SizeOfRawData = array_to_int (section->SizeOfRawData, 4);
                                    
                                    if (VirtualSize > SizeOfRawData) {
                                    
                                        VirtualAddress = array_to_int (section->VirtualAddress, 4);
                                        memset ((uintptr_t) dll_start + VirtualAddress + SizeOfRawData, 0, VirtualSize - SizeOfRawData);
                                    
                                    }
                                    
                                    if (SizeOfRawData) {
                                    
                                        VirtualAddress = array_to_int (section->VirtualAddress, 4);
                                        PointerToRawData = array_to_int (section->PointerToRawData, 4);
                                        
                                        if (lseek (fd, PointerToRawData, SEEK_SET) != PointerToRawData) {
                                            goto failed;
                                        }
                                        
                                        if (read (fd, (uint8_t *) dll_start + VirtualAddress, SizeOfRawData) != SizeOfRawData) {
                                            goto failed;
                                        }
                                    
                                    }
                                
                                }
                                
                                ImageBase = array_to_int (optional_hdr->ImageBase, 4);
                                NumberOfRvaAndSizes = array_to_int (optional_hdr->NumberOfRvaAndSizes, 4);
                                
                                if ((uintptr_t) dll_start != ImageBase) {
                                
                                    if (NumberOfRvaAndSizes > DATA_DIRECTORY_REL) {
                                    
                                        struct pe_image_data_directory *data_dir;
                                        struct pe_base_relocation *rel_block, *end_rel_block;
                                        
                                        ptrdiff_t image_diff;
                                        int lower;
                                        
                                        data_dir = (((struct pe_image_data_directory *) (optional_hdr + 1)) + DATA_DIRECTORY_REL);
                                        rel_block = ((struct pe_base_relocation *) ((uintptr_t) dll_start + array_to_int (data_dir->VirtualAddress, 4)));
                                        
                                        if (ImageBase > (uintptr_t) dll_start) {
                                        
                                            image_diff = ImageBase - (uintptr_t) dll_start;
                                            lower = 1;
                                        
                                        } else {
                                        
                                            image_diff = (uintptr_t) dll_start - ImageBase;
                                            lower = 0;
                                        
                                        }
                                        
                                        end_rel_block = rel_block + (array_to_int (data_dir->Size, 4) / sizeof (*end_rel_block));
                                        
                                        for (; rel_block < end_rel_block;) {
                                        
                                            uint16_t *cur_rel = (uint16_t *) rel_block, *end_rel;
                                            
                                            end_rel = (cur_rel + (array_to_int (rel_block->SizeOfBlock, 4) / sizeof (uint16_t)));
                                            cur_rel = (uint16_t *) (rel_block + 1);;
                                            
                                            for (; cur_rel < end_rel; cur_rel++) {
                                            
                                                uint8_t rel_type = (uint8_t) ((*cur_rel) >> 12);
                                                uint8_t *rel_target = ((uintptr_t) dll_start + array_to_int (rel_block->RVAOfBlock, 4) + ((*cur_rel) & 0x0FFF));
                                                
                                                if (rel_type == IMAGE_REL_BASED_ABSOLUTE) {
                                                    continue;
                                                }
                                                
                                                if (rel_type == IMAGE_REL_BASED_HIGHLOW) {
                                                
                                                    if (lower) {
                                                        *(uint8_t **) rel_target -= image_diff;
                                                    } else {
                                                        *(uint8_t **) rel_target += image_diff;
                                                    }
                                                
                                                } else {
                                                    continue;
                                                }
                                            
                                            }
                                            
                                            rel_block = (struct pe_base_relocation *) cur_rel;
                                        
                                        }
                                    
                                    }
                                
                                }
                                
                                if (NumberOfRvaAndSizes > DATA_DIRECTORY_EXPORT_TABLE) {
                                
                                    struct pe_image_data_directory *data_dir = (((struct pe_image_data_directory *) (optional_hdr + 1)) + DATA_DIRECTORY_EXPORT_TABLE);
                                    struct pe_export_directory *export_dir;
                                    
                                    uint32_t *funcion_table, *name_table, *thunk;
                                    uint16_t *original_table;
                                    
                                    if (array_to_int (data_dir->Size, 4) == 0) {
                                        goto failed;
                                    }
                                    
                                    export_dir = (void *) ((uintptr_t) dll_start + array_to_int (data_dir->VirtualAddress, 4));
                                    
                                    funcion_table = (void *) ((uintptr_t) dll_start + array_to_int (export_dir->ExportAddressTableRVA, 4));
                                    name_table = (void *) ((uintptr_t) dll_start + array_to_int (export_dir->NamePointerRVA, 4));
                                    original_table = (void *) ((uintptr_t) dll_start + array_to_int (export_dir->OrdinalTableRVA, 4));
                                    
                                    for (thunk = (void *) ((uintptr_t) exe_start + array_to_int (import_desc->ImportAddressTableRVA, 4)); *thunk != 0; thunk++) {
                                    
                                        if ((*thunk) & 0x80000000) {
                                            *thunk = ((uint32_t) ((uintptr_t) dll_start + funcion_table[((*thunk) & 0x7fffffff) - array_to_int (export_dir->OrdinalBase, 4)]));
                                        } else {
                                        
                                            uint8_t *hint_name = exe_start + ((*thunk) & 0x7fffffff);
                                            uint32_t count = array_to_int (export_dir->NumberOfNamePointers, 4), i;
                                            
                                            hint_name += 2;
                                            
                                            for (i = 0; i < count; i++) {
                                            
                                                if (strcmp (hint_name, (uintptr_t) dll_start + name_table[i])) {
                                                    continue;
                                                }
                                                
                                                break;
                                            
                                            }
                                            
                                            if (i == count) {
                                            
                                                printf ("failed to find %s\n", hint_name);
                                                goto failed;
                                            
                                            }
                                            
                                            *thunk = (uint32_t) ((uintptr_t) dll_start + funcion_table[original_table[i]]);
                                        
                                        }
                                    
                                    }
                                
                                }
                                
                                if (NumberOfRvaAndSizes > DATA_DIRECTORY_IMPORT_TABLE) {
                                
                                    struct pe_image_data_directory *data_dir = (((struct pe_image_data_directory *) (optional_hdr + 1)) + DATA_DIRECTORY_IMPORT_TABLE);
                                    
                                    if (array_to_int (data_dir->Size, 4) != 0) {
                                    
                                        struct pe_import_directory_table *import_desc = ((void *) ((uintptr_t) dll_start + array_to_int (data_dir->VirtualAddress, 4)));
                                        
                                        for (; array_to_int (import_desc->ImportAddressTableRVA, 4); import_desc++) {
                                        
                                            if (load_dll ((uintptr_t) dll_start, import_desc, message)) {
                                                goto failed;
                                            }
                                        
                                        }
                                    
                                    }
                                
                                }
                                
                                if (ipc_message_add_buffer (message, dll_start, SizeOfImage, (uintptr_t) dll_start)) {
                                    goto failed;
                                }
                                
                                if (array_to_int (optional_hdr->AddressOfEntryPoint, 4)) {
                                
                                    if (ipc_message_add_data (message, (uintptr_t) dll_start) || ipc_message_add_data (message, (uintptr_t) dll_start + array_to_int (optional_hdr->AddressOfEntryPoint, 4))) {
                                        goto failed;
                                    }
                                
                                }
                                
                                close (fd);
                                
                                free (section_table);
                                free (optional_hdr);
                                
                                return 0;
                            
                            }
                        
                        }
                        
                    failed:
                        
                        free (section_table);
                    
                    }
                
                }
                
                free (optional_hdr);
            
            }
        
        }
    
    }
    
    close (fd);
    return 1;

}

int load (const char *file, ipc_message_t *message) {

    struct msdos_header exec;
    
    void *addr;
    int fd;
    
    uint16_t header_size, lfanew;
    
    if ((fd = open (file, O_RDONLY)) < 0) {
        return 1;
    }
    
    if (read (fd, &exec, 2) < 2) {
    
        close (fd);
        return 1;
    
    }
    
    if (exec.e_magic[0] != 'M' || exec.e_magic[1] != 'Z') {
    
        close (fd);
        return 1;
    
    }
    
    if (read (fd, (uint8_t *) &exec + 2, 8) < 8) {
    
        close (fd);
        return 1;
    
    }
    
    header_size = array_to_int (exec.e_cparhdr, 2);
    
    if (!header_size || header_size * 16 != sizeof (exec)) {
    
        close (fd);
        return 1;
    
    }
    
    if (read (fd, (uint8_t *) &exec + 10, sizeof (exec) - 10) <  (ssize_t) (sizeof (exec) - 10)) {
    
        close (fd);
        return 1;
    
    }
    
    if (header_size >= 4 && (lfanew =  array_to_int (exec.e_lfanew, 2)) != 0) {
    
        struct pe_section_table_entry *section_table;
        struct pe_optional_header *optional_hdr;
        struct pe_header pe_hdr;
        
        if (lseek (fd, lfanew, SEEK_SET) != lfanew) {
        
            close (fd);
            return 1;
        
        }
        
        if (read (fd, &pe_hdr, 4) < 4) {
        
            close (fd);
            return 1;
        
        }
        
        if (pe_hdr.Signature[0] != 'P' || pe_hdr.Signature[1] != 'E' || pe_hdr.Signature[2] != '\0' || pe_hdr.Signature[3] != '\0') {
        
            close (fd);
            return 1;
        
        }
        
        if (read (fd, (uint8_t *) &pe_hdr + 4, sizeof (pe_hdr) - 4) < (ssize_t) (sizeof (pe_hdr) - 4)) {
        
            close (fd);
            return 1;
        
        }
        
        if (pe_hdr.Machine[0] == 0x4C && pe_hdr.Machine[1] == 0x01) {
        
            uint16_t Characteristics = array_to_int (pe_hdr.Characteristics, 2);
            
            if (!(Characteristics & IMAGE_FILE_RELOCS_STRIPPED)) {
            
                uint16_t SizeOfOptionalHeader = array_to_int (pe_hdr.SizeOfOptionalHeader, 2);
                
                if (!(optional_hdr = malloc (SizeOfOptionalHeader))) {
                
                    close (fd);
                    return 1;
                
                }
                
                if (read (fd, optional_hdr, SizeOfOptionalHeader) == SizeOfOptionalHeader) {
                
                    uint16_t Magic = array_to_int (optional_hdr->Magic, 2);
                    
                    if (Magic == IMAGE_FILE_MAGIC_I386) {
                    
                        uint16_t NumberOfSections = array_to_int (pe_hdr.NumberOfSections, 2);
                        
                        if (!(section_table = malloc (sizeof (*section_table) * NumberOfSections))) {
                        
                            free (optional_hdr);
                            
                            close (fd);
                            return 1;
                        
                        }
                        
                        if (read (fd, section_table, sizeof (*section_table) * NumberOfSections) == (ssize_t) (sizeof (*section_table) * NumberOfSections)) {
                        
                            uint32_t SizeOfImage = array_to_int (optional_hdr->SizeOfImage, 4);
                            
                            if ((addr = pe_allocate_image (SizeOfImage))) {
                            
                                struct pe_section_table_entry *section;
                                
                                uint32_t VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData;
                                uint16_t i;
                                
                                uint32_t ImageBase, NumberOfRvaAndSizes;
                                
                                for (i = 0; i < NumberOfSections; i++) {
                                
                                    section = &section_table[i];
                                    
                                    VirtualSize = array_to_int (section->VirtualSize, 4);
                                    SizeOfRawData = array_to_int (section->SizeOfRawData, 4);
                                    
                                    if (VirtualSize > SizeOfRawData) {
                                    
                                        VirtualAddress = array_to_int (section->VirtualAddress, 4);
                                        memset ((uintptr_t) addr + VirtualAddress + SizeOfRawData, 0, VirtualSize - SizeOfRawData);
                                    
                                    }
                                    
                                    if (SizeOfRawData) {
                                    
                                        VirtualAddress = array_to_int (section->VirtualAddress, 4);
                                        PointerToRawData = array_to_int (section->PointerToRawData, 4);
                                        
                                        if (lseek (fd, PointerToRawData, SEEK_SET) != PointerToRawData) {
                                            goto failed;
                                        }
                                        
                                        if (read (fd, (uint8_t *) addr + VirtualAddress, SizeOfRawData) != SizeOfRawData) {
                                            goto failed;
                                        }
                                    
                                    }
                                
                                }
                                
                                ImageBase = array_to_int (optional_hdr->ImageBase, 4);
                                NumberOfRvaAndSizes = array_to_int (optional_hdr->NumberOfRvaAndSizes, 4);
                                
                                if ((uintptr_t) addr != ImageBase) {
                                
                                    if (NumberOfRvaAndSizes > DATA_DIRECTORY_REL) {
                                    
                                        struct pe_image_data_directory *data_dir;
                                        struct pe_base_relocation *rel_block, *end_rel_block;
                                        
                                        ptrdiff_t image_diff;
                                        int lower;
                                        
                                        data_dir = (((struct pe_image_data_directory *) (optional_hdr + 1)) + DATA_DIRECTORY_REL);
                                        rel_block = ((struct pe_base_relocation *) ((uintptr_t) addr + array_to_int (data_dir->VirtualAddress, 4)));
                                        
                                        if (ImageBase > (uintptr_t) addr) {
                                        
                                            image_diff = ImageBase - (uintptr_t) addr;
                                            lower = 1;
                                        
                                        } else {
                                        
                                            image_diff = (uintptr_t) addr - ImageBase;
                                            lower = 0;
                                        
                                        }
                                        
                                        end_rel_block = rel_block + (array_to_int (data_dir->Size, 4) / sizeof (*end_rel_block));
                                        
                                        for (; rel_block < end_rel_block;) {
                                        
                                            uint16_t *cur_rel = (uint16_t *) rel_block, *end_rel;
                                            
                                            end_rel = (cur_rel + (array_to_int (rel_block->SizeOfBlock, 4) / sizeof (uint16_t)));
                                            cur_rel = (uint16_t *) (rel_block + 1);;
                                            
                                            for (; cur_rel < end_rel; cur_rel++) {
                                            
                                                uint8_t rel_type = (uint8_t) ((*cur_rel) >> 12);
                                                uint8_t *rel_target = ((uintptr_t) addr + array_to_int (rel_block->RVAOfBlock, 4) + ((*cur_rel) & 0x0FFF));
                                                
                                                if (rel_type == IMAGE_REL_BASED_ABSOLUTE) {
                                                    continue;
                                                }
                                                
                                                if (rel_type == IMAGE_REL_BASED_HIGHLOW) {
                                                
                                                    if (lower) {
                                                        *(uint8_t **) rel_target -= image_diff;
                                                    } else {
                                                        *(uint8_t **) rel_target += image_diff;
                                                    }
                                                
                                                } else {
                                                    continue;
                                                }
                                            
                                            }
                                            
                                            rel_block = (struct pe_base_relocation *) cur_rel;
                                        
                                        }
                                    
                                    }
                                
                                }
                                
                                if (ipc_message_add_buffer (message, addr, SizeOfImage, (uintptr_t) addr) || ipc_message_add_data (message, (uintptr_t) addr + array_to_int (optional_hdr->AddressOfEntryPoint, 4))) {
                                    goto failed;
                                }
                                
                                if (NumberOfRvaAndSizes > DATA_DIRECTORY_IMPORT_TABLE) {
                                
                                    struct pe_image_data_directory *data_dir = (((struct pe_image_data_directory *) (optional_hdr + 1)) + DATA_DIRECTORY_IMPORT_TABLE);
                                    
                                    if (array_to_int (data_dir->Size, 4) != 0) {
                                    
                                        struct pe_import_directory_table *import_desc = ((void *) ((uintptr_t) addr + array_to_int (data_dir->VirtualAddress, 4)));
                                        
                                        for (; array_to_int (import_desc->ImportAddressTableRVA, 4); import_desc++) {
                                        
                                            if (load_dll ((uintptr_t) addr, import_desc, message)) {
                                                goto failed;
                                            }
                                        
                                        }
                                    
                                    }
                                
                                }
                                
                                close (fd);
                                
                                free (section_table);
                                free (optional_hdr);
                                
                                message->type = IPC_MESSAGE_EXEC_NT | IPC_MESSAGE_EXEC_32BIT;
                                return 0;
                            
                            }
                        
                        }
                        
                    failed:
                        
                        free (section_table);
                    
                    }
                
                }
                
                free (optional_hdr);
            
            }
        
        }
    
    }
    
    close (fd);
    return 1;

}

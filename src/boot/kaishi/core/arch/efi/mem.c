/******************************************************************************
 * @file            mem.c
 *****************************************************************************/
#include    <kaishi/efi.h>
#include    <stddef.h>
#include    <string.h>

extern EFI_BOOT_SERVICES *BootServices;

void *alloc_mem (size_t bytes) {

    size_t *data = 0;
    
    if (EFI_ERROR (BootServices->AllocatePool (EFI_LOADER_DATA, (UINTN) bytes + sizeof (size_t), (VOID **) &data))) {
        return 0;
    }
    
    *data = bytes;
    return data + 1;

}

int free_mem (void *ptr) {
    return EFI_ERROR (BootServices->FreePool ((size_t *) ptr - 1));
}

int resize_mem (void *ptr, size_t new_size) {

    (void) new_size;
    (void) ptr;
    
    return -1;

}

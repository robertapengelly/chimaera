/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <kaishi/vfs/vfs.h>
#include    <kaishi/efi.h>
#include    <kaishi/misc.h>
#include    <kaishi/stdio.h>

#define     UEFI                        1
#include    <kaishi/disk.h>

extern void generic_main (void);

EFI_SYSTEM_TABLE *SystemTable;
EFI_BOOT_SERVICES *BootServices;

EFI_HANDLE *MainHandle;

EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;

EFI_STATUS efi_main (EFI_HANDLE EfiImageHandle, EFI_SYSTEM_TABLE *EfiSystemTable) {

    BootServices = EfiSystemTable->BootServices;
    SystemTable = EfiSystemTable;
    
    MainHandle = EfiImageHandle;
    
    ConIn = SystemTable->ConIn;
    ConOut = SystemTable->ConOut;
    
    /* Set the log level. */
    set_log_level (3);
    
    /* Initialize any COM ports that are attached. */
    rs232_port_init ();
    
    /* Initialize the virtual filesystem. */
    vfs_install ();
    
    /* Map a few directories. */
    map_vfs_directory ("/dev");
    
    /* Initialize the disks. */
    disk_create_index (EfiImageHandle);
    
    /* Run the generic code. */
    generic_main ();
    
    /* Wait for a keypress. */
    kbd_wait_key ();
    
    /* Return to the firmware. */
    return EFI_SUCCESS;

}

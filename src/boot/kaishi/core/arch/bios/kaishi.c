/******************************************************************************
 * @file            kaishi.c
 *****************************************************************************/
#include    <kaishi/vfs/vfs.h>
#include    <kaishi/disk.h>
#include    <kaishi/mem.h>
#include    <kaishi/misc.h>
#include    <kaishi/stdio.h>

#include    <inttypes.h>
extern void generic_main (void);

void kaishi_main (void) {

    /* Set the log level. */
    set_log_level (3);
    
    /* mem_init needs the very first thing called. */
    mem_init ();
    
    /* Initialize any COM ports that are attached. */
    rs232_port_init ();
    
    /* Initialize the virtual filesystem. */
    vfs_install ();
    
    /* Map a few directories. */
    map_vfs_directory ("/dev");
    
    /* Initialize the disks. */
    disk_create_index ();
    
    /* Run the generic code. */
    generic_main ();
    
    /* Wait for a keypress. */
    kbd_wait_key ();
    
    /* Cold reboot the system. */
    reboot ();

}

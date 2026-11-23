/******************************************************************************
 * @file            generic.c
 *****************************************************************************/
#include    <kaishi/vfs/node.h>
#include    <kaishi/menu.h>
#include    <kaishi/misc.h>
#include    <kaishi/stdio.h>
#include    <kaishi/stdlib.h>

extern void boot (struct menu_entry *me);

void generic_main (void) {

    struct menu_entry *me = 0;
    
    if (!init_menu ()) {
    
        /* Try parse a config file. */
        if (!parse_menu ("/boot/kaishi.cfg")) {
        
            if (!(me = run_menu ())) {
            
                kprintf ("error: failed to get menu entryn\n");
                return;
            
            }
            
            boot (me);
        
        }
    
    }

}

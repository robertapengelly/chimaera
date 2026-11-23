/******************************************************************************
 * @file            menu.h
 *****************************************************************************/
#ifndef     _MENU_H
#define     _MENU_H

#include    <kaishi/vector.h>
#include    <stdint.h>

struct menu_entry {

    const char *name;
    uint32_t hash;
    
    const char *kernel;
    uint8_t *title;
    
    struct vector vec_modules;

};

int parse_menu (const char *path);
int init_menu (void);

struct menu_entry *run_menu (void);

#endif      /* _MENU_H */

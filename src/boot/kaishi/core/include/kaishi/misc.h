/******************************************************************************
 * @file            misc.h
 *****************************************************************************/
#ifndef     _MISC_H
#define     _MISC_H

int kbd_get_key (void);
int kbd_wait_key (void);

int get_log_level (void);
void set_log_level (int level);

void reboot (void);
void rs232_port_init (void);

#include    <stdint.h>

uint64_t array_to_int (unsigned char *arr, int size);
void int_to_array (uint8_t *arr, uint64_t val, int size);

#endif      /* _MISC_H */

/******************************************************************************
 * @file            misc.h
 *****************************************************************************/
#ifndef     _MISC_H
#define     _MISC_H

char *kstrdup (const char *s);

#define     align_up(value, alignment)    \
    ((alignment) ? ((value) + ((alignment) - 1) & ~(alignment)) : value)

#define     align_down(value, alignment)    \
    ((alignment) ? ((value) & ~((alignment) - 1)) : value)

void kfree (void *ptr);

#include    <stddef.h>
void *kmalloc (size_t size);

int dprintf (const char *fmt, ...);
int kprintf (const char *fmt, ...);

void rs232_port_init (void);
#include    <stdint.h>

uint64_t array_to_int (unsigned char *arr, int size);
void int_to_array (uint8_t *arr, uint64_t val, int size);

#endif      /* _MISC_H */

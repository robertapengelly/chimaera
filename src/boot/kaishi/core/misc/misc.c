/******************************************************************************
 * @file            misc.c
 *****************************************************************************/
#include    <kaishi/misc.h>

uint64_t array_to_int (uint8_t *arr, int size) {

    uint64_t val = 0;
    int i;
    
    for (i = 0; i < size; i++) {
        val |= (uint64_t) arr[i] << (8 * i);
    }
    
    return val;

}

void int_to_array (uint8_t *arr, uint64_t val, int size) {

    int i;
    
    for (i = 0; i < size; i++) {
        arr[i] = (val >> (i * 8)) & 0xff;
    }

}

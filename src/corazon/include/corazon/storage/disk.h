/******************************************************************************
 * @file            disk.h
 *****************************************************************************/
#ifndef     _DISK_H
#define     _DISK_H

#include    <stdint.h>

__attribute__ ((packed)) typedef struct disk {

    uint8_t edd_available;
    uint8_t drive_no;
    
    uint16_t heads_per_cylinder;
    uint16_t sectors_per_track;
    
    uint32_t total_sectors;
    uint32_t sector_size;
    
    uint64_t access_remaining;
    uint64_t no_cylinders;
    
    void *data;
    int ready;

} disk_t;

#endif      /* _DISK_H */

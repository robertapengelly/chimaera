/******************************************************************************
 * @file            hdd.c
 *****************************************************************************/
#include    <corazon/storage/ata.h>
#include    <corazon/storage/disk.h>
#include    <corazon/utils/fnv1a.h>
#include    <corazon/utils/misc.h>
#include    <corazon/vfs/msdos.h>
#include    <corazon/vfs/node.h>
#include    <corazon/vfs/vfs.h>

#include    <stdint.h>
#include    <stdio.h>
#include    <string.h>

extern int cmos_get_unix_time (uint64_t *seconds);

struct probe {

    void *(*probe) (void *data, void *scratch);
    
    long start_sector;
    size_t sectors_count;

};

static struct probe probes[] = {

    {   &msdos_probe,   0,      1   },
    {   0,              0,      0   }

};

static int disk_read (void *data, void *scratch, uint64_t sector, size_t count) {

    struct fs_node *node = 0;
    struct disk *disk = 0;
    
    if (!(node = (struct fs_node *) data)) {
        return 1;
    }
    
    if (!(disk = (struct disk *) node->data)) {
        return 1;
    }
    
    return ata_accesss_sectors (sector, count, scratch, disk->data, 0);

}

static int disk_write (void *data, void *scratch, uint64_t sector, size_t count) {

    struct fs_node *node = (struct fs_node *) data;
    struct disk *disk;
    
    if (!node || !(disk = (struct disk *) node->data)) {
        return 1;
    }
    
    if (!count || sector >= disk->total_sectors || count > disk->total_sectors - sector) {
        return 1;
    }
    
    return ata_accesss_sectors (sector, count, scratch, disk->data, 1);

}

static int looks_like_mbr (void *data, void *scratch) {

    uint8_t *p = (uint8_t *) scratch;
    
    uint64_t start, end;
    int i;
    
    struct fs_node *node;
    disk_t *disk;
    
    if (!(node = (struct fs_node *) data)) {
        return 0;
    }
    
    if (!(disk = (disk_t *) node->data)) {
        return 0;
    }
    
    if (p[510] != 0x55 || p[511] != 0xAA) {
        return 0;
    }
    
    for (i = 0, p += 0x1BE; i < 4; i++, p += 16) {
    
        if (p[0] != 0x00 && p[0] != 0x80) {
            return 0;
        }
        
        start = array_to_int (p + 8, 4);
        end = array_to_int (p + 12, 4);
        
        if (start == 0 || end == 0) {
            continue;
        }
        
        if (start >= disk->total_sectors || end > disk->total_sectors - start) {
            return 0;
        }
    
    }
    
    return 1;

}

static void init_disk (void *data, uint64_t boot_hash, int disks) {

    struct probe *probe;
    disk_t *disk;
    
    uint64_t start = 0;
    uint8_t *p;
    
    size_t sector_size;
    
    struct fs_node *child = 0, *node = 0;
    uint64_t hash = 0;
    
    char *name = 0;
    int i;
    
    uint8_t *scratch = 0;
    uint8_t *temp = 0;
    
    if (!(node = (struct fs_node *) data)) {
        return;
    }
    
    if (!(disk = (struct disk *) node->data)) {
        return;
    }
    
    if (!(scratch = kmalloc (disk->sector_size))) {
        return;
    }
    
    if (!disk_read (node, scratch, (uint64_t) 0, (size_t) 1)) {
    
        if (looks_like_mbr (node, scratch)) {
        
            for (i = 0, p = scratch + 0x1BE; i < 4; i++, p += 16) {
            
                start = array_to_int (p + 8, 4);
                
                if (start > 0) {
                
                    child = 0;
                    
                    for (probe = probes; probe->probe; probe++) {
                    
                        if (!(temp = kmalloc (sector_size = disk->sector_size * probe->sectors_count))) {
                            continue;
                        }
                        
                        if (!disk_read (node, temp, (uint64_t) start + probe->start_sector, probe->sectors_count)) {
                        
                            hash = checksum64_fnv1a (temp, sector_size);
                            
                            if ((child = probe->probe (node, temp))) {
                            
                                if (!(name = kmalloc (1 + 3 + 1 + 4 + 2 + 1 + 2 + 1))) {
                                    return;
                                }
                                
                                sprintf (name, "/dev/disk%02dp%d", disks, i + 1);
                                child->flags |= INO_BLK;
                                
                                if (!child->creation_time) {
                                    child->creation_time = node->creation_time;
                                }
                                
                                vfs_mount (name, child);
                                kfree (name);
                                
                                if (p[0] == 0x80 && hash == boot_hash) {
                                    vfs_mount ("/", child);
                                }
                                
                                kfree (temp);
                                break;
                            
                            }
                        
                        }
                        
                        kfree (temp);
                    
                    }
                
                }
            
            }
        
        } else {
        
            for (probe = probes; probe->probe; probe++) {
            
                if (!(temp = kmalloc (sector_size = disk->sector_size * probe->sectors_count))) {
                    continue;
                }
                
                if (!disk_read (node, temp, (uint64_t) start + probe->start_sector, probe->sectors_count)) {
                
                    hash = checksum64_fnv1a (temp, sector_size);
                    
                    if ((child = probe->probe (node, temp))) {
                    
                        child->flags |= INO_BLK;
                        
                        if (!child->creation_time) {
                            child->creation_time = node->creation_time;
                        }
                        
                        kfree (temp);
                        break;
                    
                    }
                
                }
            
                kfree (temp);
            
            }
            
            if (child && hash == boot_hash) {
                vfs_mount ("/", child);
            }
        
        }
    
    }
    
    kfree (scratch);

}

static uintptr_t save_interrupts (void) {
    
    uintptr_t flags;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("pushfq");
#else
    __asm__ __volatile__ ("pushfd");
#endif
    
    __asm__ __volatile__ ("pop %0" : "=r" (flags) : : "memory");
    return flags;

}

static void restore_interrupts (uintptr_t flags) {
    
    __asm__ __volatile__ ("push %0" : : "r" (flags) : "memory");

#if     defined (__AMD64__)
    __asm__ __volatile__ ("popfq");
#else
    __asm__ __volatile__ ("popfd");
#endif

}

void hdd_install (uint64_t boot_hash, int disks) {

    struct ata_port_info *port_info;
    uint16_t buf[256];
    
    struct fs_node *node = 0;
    struct disk *disk = 0;
    
    char *name = 0;
    int i;
    
    uint64_t seconds = 0;
    uintptr_t flags;
    
    cmos_get_unix_time (&seconds);
    
    flags = save_interrupts ();
    __asm__ __volatile__ ("cli");
    
    for (i = 0; i < 4; i++) {
    
        if (ata_identify (i, buf) == 2) {
        
            if (!(port_info = kmalloc (sizeof (*port_info)))) {
                continue;
            }
            
            memset (port_info, 0, sizeof (*port_info));
            port_info->channel = i;
            
            if (!(ata_get_reg_ports (port_info->channel, &port_info->regs) && ata_get_irq (port_info->channel, &port_info->irq) && ata_is_slave (port_info->channel, &port_info->slave))) {
            
                dprintf ("[hdd_install] error: setup of hdd [%d] failed.\n", port_info->channel);
                
                kfree (port_info);
                continue;
            
            }
            
            if (i == 0 || i == 1) {
            
                __asm__ __volatile__ (
                    "outb %0, %1"
                    :
                    : "a" (0), "d" (0x03F6)
                );
            
            } else if (i == 2 || i == 3) {
            
                __asm__ __volatile__ (
                    "outb %0, %1"
                    :
                    : "a" (0), "d" (0x0376)
                );
            
            }
            
            if (!(disk = kmalloc (sizeof (*disk)))) {
            
                kfree (port_info);
                continue;
            
            }
            
            memset (disk, 0, sizeof (*disk));
            
            disk->sector_size = 512;
            disk->data = port_info;
            
            disk->heads_per_cylinder = buf[3];
            disk->sectors_per_track = buf[6];
            
            if ((buf[53] & 0x0001) && buf[55] && buf[56]) {
                
                disk->heads_per_cylinder = buf[55];
                disk->sectors_per_track = buf[56];
                
            }
            
            disk->total_sectors = (uint64_t) (*(uint32_t *) &buf[60]);
            
            if (!(node = kmalloc (sizeof (*node)))) {
            
                kfree (port_info);
                kfree (disk);
                
                continue;
            
            }
            
            memset (node, 0, sizeof (*node));
            
            node->creation_time = (int) seconds;
            node->flags = INO_BLK;
            
            node->data  = disk;
            node->read  = disk_read;
            node->write = disk_write;
            
            if (!(name = kmalloc (1 + 3 + 1 + 4 + 2 + 1))) {
            
                restore_interrupts (flags);
                return;
            
            }
            
            sprintf (name, "/dev/disk%02d", ++disks);
            
            vfs_mount (name, node);
            kfree (name);
            
            init_disk (node, boot_hash, disks);
        
        }
    
    }
    
    restore_interrupts (flags);

}

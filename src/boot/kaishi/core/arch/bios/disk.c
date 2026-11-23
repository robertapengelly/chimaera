/******************************************************************************
 * @file            disk.c
 *****************************************************************************/
#include    <kaishi/pc/bios.h>
#include    <kaishi/vfs/ext2.h>
#include    <kaishi/vfs/msdos.h>
#include    <kaishi/vfs/node.h>
#include    <kaishi/vfs/vfs.h>
#include    <kaishi/disk.h>
#include    <kaishi/fnv1a.h>
#include    <kaishi/misc.h>
#include    <kaishi/stdio.h>
#include    <kaishi/stdlib.h>

#include    <inttypes.h>
#include    <stddef.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <string.h>

struct probe {

    void *(*probe) (void *data, void *scratch);
    
    long start_sector;
    size_t sectors_count;

};

static struct probe probes[] = {

    {   &msdos_probe,   0,      1   },
    {   &ext2_probe,    2,      2   },
    {   0,              0,      0   }

};

__attribute__ ((packed)) struct dap {

    uint8_t size;
    uint8_t unused;
    
    uint8_t read[2];
    
    uint8_t offset[2];
    uint8_t segment[2];
    
    uint8_t sector[8];

};

__attribute__ ((packed)) struct edd_geom {

    uint8_t size[2];
    uint8_t flags[2];
    
    uint8_t no_cylinders[4];
    uint8_t heads_per_cylinder[4];
    uint8_t sectors_per_track[4];
    
    uint8_t totoal_sectors[8];
    uint8_t sector_size[2];
    
    uint8_t xxx[4];

};

extern int cmos_read (int xx);
int boot_drive = 0;

struct geom { int sectors; int heads; };
static int disks = 0;

static struct geom probe_values[] = {

    {   0x24,   0x02    },
    {   0x15,   0x02    },
    {   0x12,   0x02    },
    {   0x09,   0x02    },
    {   0x09,   0x01    },
    {   0x08,   0x02    },
    {   0x08,   0x01    }

};

static int disk_check_and_mount (void *data) {

    void *scratch = kmalloc (512);
    
    struct disk *disk;
    REGS r;
    
    struct probe *probe;
    size_t cnt, i;
    
    struct fs_node *parent = 0;
    size_t sector_size;
    
    struct fs_node *node = 0, *child = 0;
    uint64_t hash = 0;
    
    if (!(node = (struct fs_node *) data)) {
    
        kfree (scratch);
        return 1;
    
    }
    
    if (node->parent) {
    
        parent = node->parent;
        
        if (!(disk = (struct disk *) parent->data)) {
        
            kfree (scratch);
            return 1;
        
        }
    
    } else if (!(disk = (struct disk *) node->data)) {
    
        kfree (scratch);
        return 1;
    
    }
    
    cnt = sizeof (probe_values) / sizeof (probe_values[0]);
    
    for (i = 0; i < cnt; i++) {
    again:
        
        memset (&r, 0, sizeof (r));
        
        r.w.es = ((uintptr_t) scratch >> 4) & 0xffff;
        r.w.bx = (uintptr_t) scratch & 0x0f;
        
        r.w.ax = 0x0201;
        r.b.dl = disk->drive_no;
        
        r.b.cl = probe_values[i].sectors;
        r.b.dh = probe_values[i].heads - 1;
        
        int386 (0x13, &r, &r);
        
        if ((r.w.flags & 0x01)) {
        
            if (r.b.ah == 0x06) {
            
                memset (&r, 0, sizeof (r));
                
                r.b.dl = disk->drive_no;
                int386 (0x13, &r, &r);
                
                goto again;
            
            }
            
            memset (&r, 0, sizeof (r));
            
            r.b.dl = disk->drive_no;
            int386 (0x13, &r, &r);
            
            continue;
        
        }
        
        break;
    
    }
    
    kfree (scratch);
    
    if (i < cnt) {
    
        if (!parent) {
        
            parent = kmalloc (sizeof (*parent));
            memcpy (parent, node, sizeof (*parent));
        
        }
        
        disk->heads_per_cylinder = probe_values[i].heads;
        disk->sectors_per_track = probe_values[i].sectors;
        
        for (probe = probes; probe->probe; probe++) {
        
            scratch = kmalloc (sector_size = disk->sector_size * probe->sectors_count);
            
            if (!parent->read (parent, scratch, (uint64_t) probe->start_sector + 1, probe->sectors_count)) {
            
                hash = checksum64_fnv1a (scratch, sector_size);
                
                if ((child = probe->probe (parent, scratch))) {
                
                    child->checksum_hash = hash;
                    break;
                
                }
            
            }
            
            kfree (scratch);
        
        }
        
        if (child) {
        
            node->checksum_hash = child->checksum_hash;
            
            if (node->data && node->data != disk) {
                kfree (node->data);
            }
            
            node->flags   = VFS_DIRECTORY | child->flags;
            node->parent  = child->parent;
            node->data    = child->data;
            node->read    = child->read;
            node->kopen   = child->kopen;
            
            kfree (scratch);
            kfree (child);
            
            return 0;
        
        }
    
    }
    
    return 1;

}

static int disk_read (void *data, void *buf, uint64_t sector, size_t count) {

    uint8_t *scratch = buf;
    union _regs r;
    
    struct fs_node *node;
    disk_t *disk;
    
    struct dap dap = { 0 };
    int i;
    
    if (!(node = (struct fs_node *) data)) {
        return -1;
    }
    
    sector--;
    
    if (!(disk = (disk_t *) node->data)) {
        return -1;
    }
    
    while (count--) {
    
        for (i = 0; i < 5; i++) {
        
            memset (&r, 0, sizeof (r));
            
            if (disk->edd_available) {
            
                dap.size = sizeof (dap);
                
                int_to_array (dap.segment, ((uintptr_t) scratch >> 4) & 0xffff, 2);
                int_to_array (dap.offset, (uintptr_t) scratch & 0x0f, 2);
                
                int_to_array (dap.sector, sector, 8);
                int_to_array (dap.read, 1, 2);
                
                r.w.ds = ((uintptr_t) &dap >> 4) & 0xffff;
                r.w.si = (uintptr_t) &dap & 0x0f;
                
                r.b.dl = disk->drive_no;
                r.b.ah = 0x42;
                
                int386 (0x13, &r, &r);
                
                if (!(r.w.flags & 0x01)) {
                    break;
                }
            
            } else {
            
                r.w.es = ((uintptr_t) scratch >> 4) & 0xffff;
                r.w.bx = (uintptr_t) scratch & 0x0f;
                
                r.b.dl = disk->drive_no;
                r.w.ax = 0x0201;
                
                r.b.dh = (sector / disk->sectors_per_track) % disk->heads_per_cylinder;
                r.b.ch = (sector / disk->sectors_per_track) / disk->heads_per_cylinder;
                r.b.cl = (sector % disk->sectors_per_track) + 1;
                
                int386 (0x13, &r, &r);
                
                if (!(r.w.flags & 0x01)) {
                    break;
                }
                
                if (r.b.ah == 0x06) {
                    return r.b.ah;
                }
            
            }
            
            memset (&r, 0, sizeof (r));
            
            r.b.dl = disk->drive_no;
            int386 (0x13, &r, &r);
        
        }
        
        if (i == 5) {
            return -1;
        }
        
        scratch += disk->sector_size;
        sector++;
    
    }
    
    return 0;

}

static int looks_like_mbr (void *data, void *scratch) {

    uint8_t *p = (uint8_t *) scratch;
    
    uint16_t cyl, sec, head;
    uint8_t ch, cl;
    
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
    
    for (i = 0, p += 0x1BE; i < 4; i++, p += 16) {
    
        if (p[0] != 0x00 && p[0] != 0x80) {
            return 0;
        }
        
        start = array_to_int (p + 8, 4);
        end = array_to_int (p + 12, 4);
        
        if (start == 0 || end == 0) {
            continue;
        }
        
        /* For non-edd disks we'll fallback to CHS. */
        if (!disk->edd_available) {
        
            /* The start head is at offset 1. */
            head = p[1];
            
            /* Get the cylinder | sector. */
            ch = p[3];
            cl = p[2];
            
            /* The cylinder is 15 - 6. */
            cyl = (((uint16_t) (cl & 0xC0) << 2) | ch);
            
            /* The sector is 5 - 0. */
            sec = cl & 0x3F;
            
            /**
             * int 13h/ah=0x08 seems to have the cylinders 1 less than the
             * actual count so adjust for that.
             */
            if (cyl > 0) {
                cyl--;
            }
            
            /* LBA = (C * HPC + H) * SPT + (S - 1). */
            start = ((cyl * disk->heads_per_cylinder) + head) * disk->sectors_per_track + (sec - 1);
            
            /* The end head is at offset 5. */
            head = p[5];
            
            /* Get the cylinder | sector. */
            ch = p[7];
            cl = p[6];
            
            /* The cylinder is 15 - 6. */
            cyl = (((uint16_t) (cl & 0xC0) << 2) | ch);
            
            /* The sector is 5 - 0. */
            sec = cl & 0x3F;
            
            /**
             * int 13h/ah=0x08 seems to have the cylinders 1 less than the
             * actual count so adjust for that.
             */
            if (cyl > 0) {
                cyl--;
            }
            
            /* LBA = (C * HPC + H) * SPT + (S - 1). */
            end = ((cyl * disk->heads_per_cylinder) + head) * disk->sectors_per_track; + (sec - 1);
        
        }
        
        if (start > disk->total_sectors || start + end > disk->total_sectors) {
            return 0;
        }
    
    }
    
    return 1;

}

static void init_disk (int drive_no, int edd) {

    struct probe *probe;
    disk_t *disk;
    
    uint64_t start = 0;
    uint8_t *p;
    
    struct edd_geom edd_geom = { 0 };
    REGS r = { 0 };
    
    struct fs_node *local;
    size_t sector_size;
    
    struct fs_node *child = 0, *node = 0;
    uint64_t hash = 0;
    
    char *name = 0;
    int i;
    
    uint8_t *scratch = 0;
    uint8_t *temp = 0;
    
    if (edd) {
    
        int_to_array (edd_geom.size, sizeof (edd_geom), 2);
        
        r.w.ds = ((uintptr_t) &edd_geom >> 4) & 0xffff;
        r.w.si = (uintptr_t) &edd_geom & 0x0f;
        
        r.b.ah = 0x48;
        r.b.dl = drive_no;
        
        int386 (0x13, &r, &r);
        
        if ((r.w.flags & 0x01)) {
            return;
        }
        
        disk = kmalloc (sizeof (*disk));
        
        disk->drive_no = drive_no;
        disk->edd_available = 1;
        
        disk->heads_per_cylinder = array_to_int (edd_geom.heads_per_cylinder, 4);
        disk->sectors_per_track = array_to_int (edd_geom.sectors_per_track, 4);
        disk->no_cylinders = array_to_int (edd_geom.no_cylinders, 4);
        
        disk->total_sectors = array_to_int (edd_geom.totoal_sectors, 8);
        disk->sector_size = array_to_int (edd_geom.sector_size, 2);
        
        node = kmalloc (sizeof (*node));
        
        node->read = disk_read;
        node->data = disk;
        
        scratch = kmalloc (disk->sector_size);
        
        if (disk_read (node, scratch, (uint64_t) 1, (size_t) 1)) {
        
            kfree (scratch);
            return;
        
        }
        
        if (looks_like_mbr (node, scratch)) {
        
            name = kmalloc (1 + 3 + 1 + 4 + 2 + 1);
            sprintf (name, "/dev/disk%02d", ++disks);
            
            vfs_mount (name, node);
            kfree (name);
            
            for (i = 0, p = scratch + 0x1BE; i < 4; i++, p += 16) {
            
                start = array_to_int (p + 8, 4);
                
                if (start > 0) {
                
                    for (probe = probes; probe->probe; probe++) {
                    
                        temp = kmalloc (sector_size = disk->sector_size * probe->sectors_count);
                        
                        if (!disk_read (node, temp, (uint64_t) start + probe->start_sector + 1, probe->sectors_count)) {
                        
                            hash = checksum64_fnv1a (temp, sector_size);
                            
                            if ((child = probe->probe (node, temp))) {
                            
                                child->checksum_hash = hash;
                                
                                name = kmalloc (1 + 3 + 1 + 4 + 2 + 1 + 2 + 1);
                                sprintf (name, "/dev/disk%02dp%d", disks, i + 1);
                                
                                child->flags |= VFS_DIRECTORY;
                                vfs_mount (name, child);
                                
                                kfree (name);
                                
                                if (p[0] == 0x80 && drive_no == boot_drive) {
                                    vfs_mount ("/", child);
                                }
                                
                                break;
                            
                            }
                        
                        }
                        
                        kfree (temp);
                    
                    }
                    
                    if (child) {
                        kfree (temp);
                    }
                
                }
            
            }
            
            kfree (scratch);
        
        } else {
        
            kfree (scratch);
            
            name = kmalloc (1 + 3 + 1 + 4 + 4 + 1);
            sprintf (name, "/dev/disk%02d", ++disks);
            
            local = node;
            
            for (probe = probes; probe->probe; probe++) {
            
                scratch = kmalloc (sector_size = disk->sector_size * probe->sectors_count);
                
                if (!disk_read (node, scratch, (uint64_t) start + probe->start_sector + 1, probe->sectors_count)) {
                
                    hash = checksum64_fnv1a (scratch, sector_size);
                    
                    if ((child = probe->probe (node, scratch))) {
                    
                        child->checksum_hash = hash;
                        
                        child->flags |= VFS_DIRECTORY;
                        local = child;
                        
                        break;
                    
                    }
                
                }
                
                kfree (scratch);
            
            }
            
            if (child) {
                kfree (scratch);
            }
            
            vfs_mount (name, local);
            kfree (name);
            
            if (drive_no == boot_drive) {
                vfs_mount ("/", local);
            }
        
        }
    
    } else {
    
        r.w.ax = 0x0800;
        r.b.dl = drive_no;
        
        int386 (0x13, &r, &r);
        
        if ((r.w.flags & 0x01)) {
            return;
        }
        
        disk = kmalloc (sizeof (*disk));
        disk->drive_no = drive_no;
        
        disk->heads_per_cylinder = r.b.dh + 1;
        disk->sectors_per_track = (r.w.cx & 0x3F);
        disk->no_cylinders = (((r.w.cx & 0xC0) << 2) | (r.w.cx >> 8)) + 1;
        
        disk->total_sectors = disk->no_cylinders * disk->heads_per_cylinder * disk->sectors_per_track;
        disk->sector_size = 512;
        
        node = kmalloc (sizeof (*node));
        
        node->read = disk_read;
        node->data = disk;
        
        scratch = kmalloc (disk->sector_size);
        
        if (disk_read (node, scratch, (uint64_t) 1, (size_t) 1)) {
        
            kfree (scratch);
            return;
        
        }
        
        if (looks_like_mbr (node, scratch)) {
        
            name = kmalloc (1 + 3 + 1 + 4 + 2 + 1);
            sprintf (name, "/dev/disk%02d", ++disks);
            
            vfs_mount (name, node);
            kfree (name);
            
            for (i = 0, p = scratch + 0x1BE; i < 4; i++, p += 16) {
            
                start = array_to_int (p + 8, 4);
                
                if (start > 0) {
                
                    for (probe = probes; probe->probe; probe++) {
                    
                        temp = kmalloc (sector_size = disk->sector_size * probe->sectors_count);
                        
                        if (!disk_read (node, temp, (uint64_t) start + probe->start_sector + 1, probe->sectors_count)) {
                        
                            hash = checksum64_fnv1a (temp, sector_size);
                            
                            if ((child = probe->probe (node, temp))) {
                            
                                child->checksum_hash = hash;
                                
                                name = kmalloc (1 + 3 + 1 + 4 + 2 + 1 + 2 + 1);
                                sprintf (name, "/dev/disk%02dp%d", disks, i + 1);
                                
                                child->flags |= VFS_DIRECTORY;
                                vfs_mount (name, child);
                                
                                kfree (name);
                                
                                if (p[0] == 0x80 && drive_no == boot_drive) {
                                    vfs_mount ("/", child);
                                }
                                
                                break;
                            
                            }
                        
                        }
                        
                        kfree (temp);
                    
                    }
                    
                    if (child) {
                        kfree (temp);
                    }
                
                }
            
            }
            
            kfree (scratch);
        
        } else {
        
            kfree (scratch);
            
            name = kmalloc (1 + 3 + 1 + 4 + 4 + 1);
            sprintf (name, "/dev/disk%02d", ++disks);
            
            local = node;
            
            for (probe = probes; probe->probe; probe++) {
            
                scratch = kmalloc (sector_size = disk->sector_size * probe->sectors_count);
                
                if (!disk_read (node, scratch, (uint64_t) start + probe->start_sector + 1, probe->sectors_count)) {
                
                    hash = checksum64_fnv1a (scratch, sector_size);
                    
                    if ((child = probe->probe (node, scratch))) {
                    
                        child->checksum_hash = hash;
                        
                        child->flags |= VFS_DIRECTORY;
                        local = child;
                        
                        break;
                    
                    }
                
                }
                
                kfree (scratch);
            
            }
            
            if (child) {
                kfree (scratch);
            }
            
            vfs_mount (name, local);
            kfree (name);
            
            if (drive_no == boot_drive) {
                vfs_mount ("/", local);
            }
        
        }
    
    }

}

void disk_create_index (void) {

    int i, j;
    
    struct disk *disk;
    REGS r;
    
    struct fs_node *node = 0;
    char *name = 0;
    
    for (i = cmos_read (0x10), j = 0; i & 0xF0; i <<= 4, j++) {
    
        disk = kmalloc (sizeof (*disk));
        
        disk->sector_size = 512;
        disk->drive_no = j;
        
        node = kmalloc (sizeof (*node));
        
        node->read = disk_read;
        node->data = disk;
        
        node->check_and_mount = disk_check_and_mount;
        node->flags = VFS_DIRECTORY;
        
        name = kmalloc (1 + 3 + 1 + 4 + 2 + 1);
        sprintf (name, "/dev/disk%02d", ++disks);
        
        vfs_mount (name, node);
        
        if (j == boot_drive) {
            vfs_mount ("/", node);
        }
    
    }
    
    for (i = 0x80; i < 0xE0; i++) {
    
        memset (&r, 0, sizeof (r));
        
        r.w.ax = 0x4100;
        r.w.bx = 0x55AA;
        
        r.b.dl = i;
        int386 (0x13, &r, &r);
        
        if (!(r.w.flags & 0x01) && r.w.bx == 0xAA55 && r.w.cx & 0x01) {
        
            init_disk (i, 1);
            continue;
        
        }
        
        memset (&r, 0, sizeof (r));
        
        r.w.ax = 0x0800;
        r.b.dl = i;
        
        int386 (0x13, &r, &r);
        
        if (!(r.w.flags & 0x01)) {
            init_disk (i, 0);
        }
    
    }

}

uint32_t disk_get_sector_size (void *data) {

    disk_t *disk;
    
    if (!(disk = (disk_t *) data)) {
        return 0;
    }
    
    return disk->sector_size;

}

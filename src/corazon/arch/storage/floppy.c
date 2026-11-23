/******************************************************************************
 * @file            floppy.c
 *****************************************************************************/
#include    <corazon/storage/disk.h>
#include    <corazon/tasking/irq.h>
#include    <corazon/tasking/process.h>
#include    <corazon/utils/fnv1a.h>
#include    <corazon/utils/misc.h>
#include    <corazon/vfs/msdos.h>
#include    <corazon/vfs/node.h>
#include    <corazon/vfs/vfs.h>
#include    <corazon/acpi.h>
#include    <corazon/dma.h>
#include    <corazon/mmu.h>
#include    <corazon/timer.h>

#include    <errno.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <string.h>

static uint8_t floppy_selected_drive = 0xFF;
static uint8_t floppy_calibrated[4] = { 0 };

static void *dma_buffer = 0x10000;

static int floppy_irq_installed = 0;
static int floppy_ioapic_routed = 0;

static volatile uint32_t floppy_irq_counter = 0;

static int floppy_irq_handler (struct regs *r) {

    (void) r;
    
    floppy_irq_counter++;
    irq_ack ();
    
    if (floppy_ioapic_routed) {
        acpi_lapic_ack ();
    }
    
    return 1;

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

static int floppy_wait_irq (uint32_t previous, uint32_t timeout) {

    uint64_t deadline = get_uptime_milliseconds () + timeout;
    
    struct thread *thread = thread_get_current ();
    enum thread_state state = TS_RUN;
    
    uintptr_t flags = save_interrupts ();
    
    if (thread) {
    
        state = thread->state;
        thread->state = TS_UNINTERRUPTIBLE;
    
    }
    
    __asm__ __volatile__ ("sti");
    
    while (floppy_irq_counter == previous) {
    
        if (get_uptime_milliseconds () >= deadline) {
        
            restore_interrupts (flags);
            
            if (thread) {
                thread->state = state;
            }
            
            return 0;
        
        }
        
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("hlt");
#endif
    
    }
    
    restore_interrupts (flags);
    
    if (thread) {
        thread->state = state;
    }
    
    return 1;

}

struct probe {

    void *(*probe) (void *data, void *scratch);
    
    long start_sector;
    size_t sectors_count;

};

extern int cmos_get_unix_time (uint64_t *seconds);
extern uint8_t cmos_read (uint8_t offset);

static struct probe probes[] = {

    {   &msdos_probe,   0,      1   },
    {   0,              0,      0   }

};

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

typedef enum { READ, WRITE } RW_OPERATION;
#define     IS_BIT_SET(value, pos)      ((value) & (1U << (pos)))

static void floppy_lba_to_chs (struct disk *disk, uint64_t lba, uint8_t *head, uint8_t *track, uint8_t *sector) {

    *sector = (lba % disk->sectors_per_track) + 1;
    *head   = (lba / disk->sectors_per_track) % disk->heads_per_cylinder;
    *track  =  lba / (disk->sectors_per_track * disk->heads_per_cylinder);

}

static uint8_t floppy_read_status (void) {

    uint8_t status;
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "d" (0x03F4)
    );
    
    return status;

}

static int floppy_send_command (uint8_t cmd) {

    uintptr_t flags = save_interrupts ();
    uint32_t i;
    
    __asm__ __volatile__ ("cli");
    
    for (i = 0; i < 100000; i++) {
    
        uint8_t status = floppy_read_status ();
        
        if ((status & 0x80) && !(status & 0x40)) {
        
            __asm__ __volatile__ (
                "outb %0, %1"
                :
                : "a" (cmd), "d" (0x03F5)
            );
            
            restore_interrupts (flags);
            return 1;
        
        }
    
    }
    
    restore_interrupts (flags);
    return 0;

}

static void floppy_write_dor (uint8_t val) {

    uintptr_t flags = save_interrupts ();
    __asm__ __volatile__ ("cli");
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        :
        "a" (val), "d" (0x03F2)
    );
    
    restore_interrupts (flags);

}

static void floppy_motor_on (struct disk *disk) {

    struct thread *thread = thread_get_current ();
    enum thread_state state = TS_RUN;
    
    uintptr_t flags = save_interrupts ();
    uint32_t motor;
    
    __asm__ __volatile__ ("cli");
    
    if (disk->ready && floppy_selected_drive == disk->drive_no) {
    
        restore_interrupts (flags);
        return;
    
    }
    
    switch (disk->drive_no) {
    
        case 0: motor = 0x10; break;
        case 1: motor = 0x20; break;
        case 2: motor = 0x40; break;
        case 3: motor = 0x80; break;
    
    }
    
    floppy_write_dor (disk->drive_no | motor | 0x04 | 0x08);
    
    if (thread) {
    
        state = thread->state;
        thread->state = TS_UNINTERRUPTIBLE;
    
    }
    
    __asm__ __volatile__ ("sti");
    delay (300);
    
    __asm__ __volatile__ ("cli");
    
    if (thread) {
        thread->state = state;
    }
    
    disk->ready = 1;
    floppy_selected_drive = disk->drive_no;
    
    restore_interrupts (flags);

}

static int floppy_disk_changed (struct disk *disk) {

    uintptr_t flags = save_interrupts ();
    uint8_t status;
    
    __asm__ __volatile__ ("cli");
    floppy_motor_on (disk);
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "d" (0x03F7)
    );
    
    restore_interrupts (flags);
    return (status & 0x80) != 0;

}

static int floppy_read_data (uint8_t *value) {

    uintptr_t flags = save_interrupts ();
    uint32_t i;
    
    __asm__ __volatile__ ("cli");
    
    for (i = 0; i < 100000; i++) {
    
        uint8_t status = floppy_read_status ();
        
        if ((status & 0x80) && (status & 0x40)) {
        
            uint8_t ret;
            
            __asm__ __volatile__ (
                "inb %1, %0"
                : "=a" (ret)
                : "d" (0x03F5)
            );
            
            *value = ret;
            
            restore_interrupts (flags);
            return 1;
        
        }
    
    }
    
    restore_interrupts (flags);
    return 0;

}

static int floppy_check_int (uint8_t *st0, uint8_t *cyl) {

    if (!floppy_send_command (0x08)) {
        return 0;
    }
    
    if (!floppy_read_data (st0) || !floppy_read_data (cyl)) {
        return 0;
    }
    
    return 1;

}

static int floppy_calibrate (struct disk *disk) {

    uint8_t st0, cyl, timeout = 10;
    uint32_t previous;
    
    floppy_motor_on (disk);
    
    do {
    
        previous = floppy_irq_counter;
        
        if (!floppy_send_command (0x07) || !floppy_send_command (disk->drive_no)) {
            return 0;
        }
        
        if (!floppy_wait_irq (previous, 500)) {
            return 0;
        }
        
        if (!floppy_check_int (&st0, &cyl)) {
            return 0;
        }
        
        timeout--;
        
        if (timeout == 0) {
            return 0;
        }
    
    } while ((st0 & 0xC0) || !IS_BIT_SET (st0, 5) || cyl != 0);
    
    return 1;

}

static int floppy_seek (struct disk *disk, uint8_t cyl, uint8_t head) {

    uint8_t st0, cyl0, timeout = 10;
    uint32_t previous;
    
    disk->access_remaining++;
    
    if (!floppy_calibrated[disk->drive_no]) {
    
        if (!(floppy_calibrate (disk))) {
        
            disk->access_remaining--;
            return 2;
        
        }
        
        floppy_calibrated[disk->drive_no] = 1;
    
    }
    
    floppy_motor_on (disk);
    
    do {
    
        previous = floppy_irq_counter;
        
        if (!floppy_send_command (0x0F) || !floppy_send_command (head << 2 | disk->drive_no) || !floppy_send_command (cyl)) {
        
            disk->access_remaining--;
            return 1;
        
        }
        
        if (!floppy_wait_irq (previous, 2000)) {
        
            disk->access_remaining--;
            return 1;
        
        }
        
        if (!floppy_check_int (&st0, &cyl0)) {
        
            disk->access_remaining--;
            return 1;
        
        }
        
        timeout--;
        
        if (timeout == 0) {
        
            disk->access_remaining--;
            return 1;
        
        }
    
    } while ((st0 & 0xC0) || !IS_BIT_SET (st0, 5) || cyl0 != cyl);
    
    disk->access_remaining--;
    return 0;

}

static int floppy_transfer_sector (struct disk *disk, uint8_t head, uint8_t track, uint8_t sector, uint8_t count, RW_OPERATION operation) {

    uint8_t val[7], j;
    uint32_t previous;
    
    int sent;
    
    if (!count || count > 127) {
        return 0;
    }
    
    disk->access_remaining++;
    
    floppy_motor_on (disk);
    previous = floppy_irq_counter;
    
    if (operation == READ) {
    
        dma_read (dma_buffer, count * 512, &dma_channel[2], DMA_SINGLE);
        sent = floppy_send_command (0x06 | 0x40);
    
    } else if (operation == WRITE) {
    
        dma_write (dma_buffer, count * 512, &dma_channel[2], DMA_SINGLE);
        sent = floppy_send_command (0x05 | 0x40);
    
    }
    
    if (!sent || !floppy_send_command (head << 2 | disk->drive_no) || !floppy_send_command (track) || !floppy_send_command (head) || !floppy_send_command (sector) || !floppy_send_command (2) || !floppy_send_command (sector + count - 1) || !floppy_send_command (27) || !floppy_send_command (0xFF)) {
    
        disk->access_remaining--;
        return 0;
    
    }
    
    if (!floppy_wait_irq (previous, 2000)) {
    
        disk->access_remaining--;
        return 0;
    
    }
    
    for (j = 0; j < 7; j++) {
    
        if (!floppy_read_data (&val[j])) {
        
            disk->access_remaining--;
            return 0;
        
        }
    
    }
    
    disk->access_remaining--;
    return !(val[0] & 0xC0) && !val[1] && !val[2] && val[6] == 2;

}

static int floppy_read (void *data, uint64_t lba, uint8_t count) {

    uint8_t head = 0, track = 0, sector = 1;
    
    struct fs_node *node = 0;
    struct disk *disk = 0;
    
    uint32_t timeout = 5;
    
    if (!(node = (struct fs_node *) data)) {
        return 1;
    }
    
    if (!(disk = (struct disk *) node->data)) {
        return 1;
    }
    
    if (floppy_disk_changed (disk)) {
    
        floppy_calibrated[disk->drive_no] = 0;
        
        disk->heads_per_cylinder = 0;
        disk->sectors_per_track = 0;
        
        return 1;
    
    }
    
    if (!count || !disk->heads_per_cylinder || !disk->sectors_per_track) {
        return 1;
    }
    
    floppy_lba_to_chs (disk, lba, &head, &track, &sector);
    
    if ((uint16_t) sector + count - 1 > disk->sectors_per_track) {
        return 1;
    }
    
    if (floppy_seek (disk, track, head)) {
        return 1;
    }
    
    while (!floppy_transfer_sector (disk, head, track, sector, count, READ)) {
    
        timeout--;
        
        if (timeout == 0) {
            return 1;
        }
    
    }
    
    return 0;

}

static int floppy_check_and_mount (void *data) {

    struct disk *disk;
    int changed;
    
    struct probe *probe;
    size_t cnt, i;
    
    struct fs_node *parent = 0;
    uint8_t head, track, sector;
    
    struct fs_node *node = 0, *child = 0;
    void *scratch;
    
    if (!(node = (struct fs_node *) data)) {
        return 1;
    }
    
    if (node->parent) {
    
        parent = node->parent;
        
        if (!(disk = (struct disk *) parent->data)) {
            return 1;
        }
    
    } else if (!(disk = (struct disk *) node->data)) {
        return 1;
    }
    
    changed = floppy_disk_changed (disk);
    
    if (node->parent && !changed && disk->heads_per_cylinder && disk->sectors_per_track) {
        return 0;
    }
    
    if (changed) {
    
        floppy_calibrated[disk->drive_no] = 0;
        
        disk->heads_per_cylinder = 0;
        disk->sectors_per_track = 0;
        
        /* A move to cylinder 1 clears the disk-change latch if media is present. */
        if (floppy_seek (disk, 1, 0) || floppy_disk_changed (disk)) {
            return 1;
        }
    
    }
    
    disk->heads_per_cylinder = 0;
    disk->sectors_per_track = 0;
    
    cnt = sizeof (probe_values) / sizeof (probe_values[0]);
    
    for (i = 0; i < cnt; i++) {
    
        head = probe_values[i].heads - 1;
        track = 0;
        sector = probe_values[i].sectors;
        
        if (floppy_seek (disk, track, head)){
            continue;    
        }
        
        if (!floppy_transfer_sector (disk, head, track, sector, 1, READ)) {
            continue;
        }
        
        break;
    
    }
    
    if (i < cnt) {
    
        if (!parent) {
        
            if (!(parent = kmalloc (sizeof (*parent)))) {
                return -ENOMEM;
            }
            
            memcpy (parent, node, sizeof (*parent));
        
        }
        
        disk->heads_per_cylinder = probe_values[i].heads;
        disk->sectors_per_track = probe_values[i].sectors;
        
        for (probe = probes; probe->probe; probe++) {
        
            if (!(scratch = kmalloc (disk->sector_size * probe->sectors_count))) {
                return -ENOMEM;
            }
            
            if (!parent->read (parent, scratch, (uint64_t) probe->start_sector, probe->sectors_count)) {
            
                if ((child = probe->probe (parent, scratch))) {
                    break;
                }
            
            }
            
            kfree (scratch);
        
        }
        
        if (child) {
        
            if (node->data && node->data != disk) {
                kfree (node->data);
            }
            
            node->flags       = INO_BLK | child->flags;
            node->parent      = child->parent;
            node->data        = child->data;
            node->read        = child->read;
            node->write       = child->write;
            
            node->kopen_flags = child->kopen_flags;
            node->kopen       = child->kopen;
            
            node->kremove     = child->kremove;
            node->kmkdir      = child->kmkdir;
            node->krename     = child->krename;
            
            kfree (child);
            return 0;
        
        }
    
    }
    
    return 1;

}

static int disk_read (void *data, void *scratch, uint64_t sector, size_t count) {

    struct fs_node *node = 0;
    struct disk *disk = 0;
    
    int ret = 1, n;
    
    if (!(node = (struct fs_node *) data)) {
        return 1;
    }
    
    if (!(disk = (struct disk *) node->data)) {
        return 1;
    }
    
    if (floppy_disk_changed (disk)) {
    
        floppy_calibrated[disk->drive_no] = 0;
        
        disk->heads_per_cylinder = 0;
        disk->sectors_per_track = 0;
        
        return 6;
    
    }
    
    if (!count || count > 127 || !disk->sectors_per_track) {
        return 1;
    }
    
    while (count) {
    
        size_t chunk = disk->sectors_per_track - (sector % disk->sectors_per_track);
        
        if (chunk > count) {
            chunk = count;
        }
        
        for (n = 0; n < 3; n++) {
        
            ret = floppy_read (node, sector, (uint8_t) chunk);
            
            if (!ret) {
                break;
            }
        
            if (floppy_disk_changed (disk)) {
            
                floppy_calibrated[disk->drive_no] = 0;
                
                disk->heads_per_cylinder = 0;
                disk->sectors_per_track = 0;
                
                return 6;
            
            }
        
        }
        
        if (ret) {
            return ret;
        }
        
        memcpy (scratch, dma_buffer, chunk * 512);
        scratch = (uint8_t *) scratch + chunk * 512;
        
        sector += chunk;
        count -= chunk;
    
    }
    
    return 0;

}

static int disk_write (void *data, void *scratch, uint64_t sector, size_t count) {

    struct fs_node *node = (struct fs_node *) data;
    struct disk *disk;
    
    uint8_t head, track, sec;
    int n;
    
    if (!node || !(disk = (struct disk *) node->data)) {
        return 1;
    }
    
    if (!count || count > 127 || !disk->heads_per_cylinder || !disk->sectors_per_track) {
        return 1;
    }
    
    while (count) {
    
        if (floppy_disk_changed (disk)) {
        
            floppy_calibrated[disk->drive_no] = 0;
            
            disk->heads_per_cylinder = 0;
            disk->sectors_per_track = 0;
            
            return 6;
        
        }
        
        floppy_lba_to_chs (disk, sector, &head, &track, &sec);
        
        if (floppy_seek (disk, track, head)) {
            return 1;
        }
        
        for (n = 0; n < 5; n++) {
        
            memcpy (dma_buffer, scratch, 512);
            
            if (floppy_transfer_sector (disk, head, track, sec, 1, WRITE)) {
                break;
            }
        
        }
        
        if (n == 5) {
            return 1;
        }
        
        scratch = (uint8_t *) scratch + 512;
        sector++;
        count--;
    
    }
    
    return 0;

}

int init_floppies (uint64_t boot_checksum) {

    char *name = 0;
    int i, j;
    
    struct fs_node *node = 0;
    struct disk *disk = 0;
    
    uint64_t seconds = 0;
    uintptr_t flags;
    
    cmos_get_unix_time (&seconds);
    
    flags = save_interrupts ();
    __asm__ __volatile__ ("cli");
    
    if (!floppy_irq_installed) {
    
        irq_install_handler (6, floppy_irq_handler, "floppy");
        
#if     defined (__AMD64__)
        floppy_ioapic_routed = acpi_route_legacy_irq (6);
#endif
        
        if (!floppy_ioapic_routed) {
        
            uint8_t mask;
            
            __asm__ __volatile__ (
                "inb %1, %0"
                : "=a" (mask)
                : "Nd" ((uint16_t) 0x21)
            );
            
            mask &= ~0x40;
            
            __asm__ __volatile__ (
                "outb %0, %1"
                :
                : "a" (mask), "Nd" ((uint16_t) 0x21)
            );
        
        }
        
        floppy_irq_installed = 1;
    
    }
    
    for (i = cmos_read (0x10), j = 0; i & 0xF0; i <<= 4, j++) {
    
        if (!(disk = kmalloc (sizeof (*disk)))) {
        
            restore_interrupts (flags);
            return 0;
        
        }
        
        memset (disk, 0, sizeof (*disk));
        
        disk->sector_size = 512;
        disk->drive_no = j;
        
        if (!(node = kmalloc (sizeof (*node)))) {
        
            restore_interrupts (flags);
            return 0;
        
        }
        
        memset (node, 0, sizeof (*node));
        
        node->write = disk_write;
        node->read  = disk_read;
        node->data  = disk;
        
        node->check_and_mount = floppy_check_and_mount;
        node->flags = INO_BLK;
        
        node->creation_time = (int) seconds;
        
        if (!(name = kmalloc (1 + 3 + 1 + 4 + 2 + 1))) {
        
            restore_interrupts (flags);
            return 0;
        
        }
        
        sprintf (name, "/dev/disk%02d", ++disks);
        vfs_mount (name, node);
        
        if (!floppy_seek (disk, 0, 0)) {
        
            if (floppy_transfer_sector (disk, 0, 0, 1, 1, READ)) {
            
                uint64_t hash = checksum64_fnv1a (dma_buffer, disk->sector_size);
                
                if (boot_checksum == hash) {
                    vfs_mount ("/", node);
                }
            
            }
        
        }
    
    }
    
    restore_interrupts (flags);
    return disks;

}

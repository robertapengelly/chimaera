/******************************************************************************
 * @file            ata.c
 *****************************************************************************/
#include    <corazon/storage/ata.h>
#include    <corazon/tasking/irq.h>
#include    <corazon/tasking/process.h>
#include    <corazon/utils/misc.h>
#include    <corazon/acpi.h>
#include    <corazon/timer.h>

#include    <stdint.h>

static int ata_irq_installed = 0;
static int ata_ioapic_routed[2] = { 0 };

static volatile uint32_t ata_irq_counter[2] = { 0 };

static int ata_irq_handler_primary (struct regs *r) {

    (void) r;
    
    ata_irq_counter[0]++;
    irq_ack ();
    
    if (ata_ioapic_routed[0]) {
        acpi_lapic_ack ();
    }
    
    return 1;

}

static int ata_irq_handler_secondary (struct regs *r) {

    (void) r;
    
    ata_irq_counter[1]++;
    irq_ack ();
    
    if (ata_ioapic_routed[1]) {
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

static void ata_unmask_pic_irq (uint8_t irq) {

    uintptr_t flags = save_interrupts ();
    uint8_t mask;
    
    __asm__ __volatile__ ("cli");
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (mask)
        : "Nd" ((uint16_t) 0x21)
    );
    
    mask &= ~0x04;
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (mask), "Nd" ((uint16_t) 0x21)
    );
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (mask)
        : "Nd" ((uint16_t) 0xA1)
    );
    
    mask &= ~(1 << (irq - 8));
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (mask), "Nd" ((uint16_t) 0xA1)
    );
    
    restore_interrupts (flags);

}

static void wait400NS (uint16_t p) {

    uintptr_t flags = save_interrupts ();
    uint8_t tmp;
    
    __asm__ __volatile__ ("cli");
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (tmp)
        : "d" (p)
    );
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (tmp)
        : "d" (p)
    );
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (tmp)
        : "d" (p)
    );
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (tmp)
        : "d" (p)
    );
    
    restore_interrupts (flags);

}

/* Keep the current thread running while the caller holds filesystem locks. */
static void ata_delay (uint32_t milliseconds) {

    struct thread *thread = thread_get_current ();
    enum thread_state state = TS_RUN;
    
    uintptr_t flags = save_interrupts ();
    
    __asm__ __volatile__ ("cli");
    
    if (thread) {
    
        state = thread->state;
        thread->state = TS_UNINTERRUPTIBLE;
    
    }
    
    __asm__ __volatile__ ("sti");
    delay (milliseconds);
    
    __asm__ __volatile__ ("cli");
    
    if (thread) {
        thread->state = state;
    }
    
    restore_interrupts (flags);

}

static int wait_busy (struct ata_regs *regs) {

    uint8_t status;
    
    uintptr_t flags = save_interrupts ();;
    int i;
    
    __asm__ __volatile__ ("cli");
    wait400NS (regs->status);
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "d" (regs->status)
    );
    
    for (i = 0; i < 3000; i++) {
    
        if (!(status & 0x80)) {
        
            restore_interrupts (flags);
            return 1;
        
        }
        
        ata_delay (10);
        
        __asm__ __volatile__ (
            "inb %1, %0"
            : "=a" (status)
            : "d" (regs->status)
        );
    
    }
    
    restore_interrupts (flags);
    return 0;

}

static void setup_lba (struct ata_port_info *port, int lba48, uint64_t sector, size_t count) {

    if (lba48) {
    
        __asm__ __volatile__ (
            "outb %0, %1"
            :
            : "a" ((count >> 8) & 0xFF), "d" (port->regs.sectors)
        );
        
        __asm__ __volatile__ (
            "outb %0, %1"
            :
            : "a" ((sector >> 24) & 0xFF), "d" (port->regs.lbalo)
        );
    
    }
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (count & 0xFF), "d" (port->regs.sectors)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (sector & 0xFF), "d" (port->regs.lbalo)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" ((sector >> 8) & 0xFF), "d" (port->regs.lbamid)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" ((sector >> 16) & 0xFF), "d" (port->regs.lbahi)
    );

}

static int wait_irq (struct ata_port_info *port, uint32_t previous, uint8_t err_mask, uint8_t *status) {

    uint64_t deadline = get_uptime_milliseconds () + 2000;
    int index = port->irq - 14;
    
    uint8_t drive_status;
    
    struct thread *thread = thread_get_current ();
    enum thread_state state = TS_RUN;
    
    uintptr_t flags = save_interrupts ();
    
    if (thread) {
    
        state = thread->state;
        thread->state = TS_UNINTERRUPTIBLE;
    
    }
    
    __asm__ __volatile__ ("sti");
    
    while (ata_irq_counter[index] == previous) {
    
        if (get_uptime_milliseconds () >= deadline) {
        
            if (status) {
                *status = 0xFF;
            }
            
            if (thread) {
                thread->state = state;
            }
            
            restore_interrupts (flags);
            return 0;
        
        }
        
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("hlt");
#endif
    
    }
    
    __asm__ __volatile__ ("cli");
    wait400NS (port->regs.altstatus);
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (drive_status)
        : "d" (port->regs.altstatus)
    );
    
    if (status) {
        *status = drive_status;
    }
    
    if (thread) {
        thread->state = state;
    }
    
    restore_interrupts (flags);
    return !(drive_status & err_mask);

}

static int poll (struct ata_port_info *port, uint8_t err_mask, uint8_t good_mask, uint8_t *status) {

    uint8_t port_val;
    
    uintptr_t flags = save_interrupts ();;
    uint32_t i;
    
    __asm__ __volatile__ ("cli");
    wait400NS (port->regs.status);
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (port_val)
        : "d" (port->regs.status)
    );
    
    for (i = 0; i < 3000; i++) {
    
        if (!(port_val & 0x80)) {
        
            if (port_val & err_mask) {
            
                if (status) {
                    *status = port_val;
                }
                
                restore_interrupts (flags);
                return 0;
            
            }
            
            if (port_val & good_mask) {
            
                if (status) {
                    *status = port_val;
                }
                
                restore_interrupts (flags);
                return 1;
            
            }
        
        }
        
        ata_delay (10);
        
        __asm__ __volatile__ (
            "inb %1, %0"
            : "=a" (port_val)
            : "d" (port->regs.status)
        );
    
    }
    
    if (status) {
        *status = 0xFF;
    }
    
    restore_interrupts (flags);
    return 0;

}

static void select_drive (struct ata_port_info *port, int lba48, uint64_t sector) {

    if (lba48) {
    
        __asm__ __volatile__ (
            "outb %0, %1"
            :
            : "a" (port->slave ? 0x50 : 0x40), "d" (port->regs.drive)
        );
    
    } else {
    
        __asm__ __volatile__ (
            "outb %0, %1"
            :
            : "a" ((port->slave ? 0xF0 : 0xE0) | ((sector >> 24) & 0x0F)), "d" (port->regs.drive)
        );
    
    }

}

static void ata_soft_reset (struct ata_port_info *port) {

    uintptr_t flags = save_interrupts ();
    
    uint8_t devcontrol = 0x04;
    int i;
    
    __asm__ __volatile__ ("cli");
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (devcontrol), "d" (port->regs.devcontrol)
    );
    
    for (i = 0; i < 16; i++) {
        wait400NS (port->regs.altstatus);
    }
    
    devcontrol = 0;
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (devcontrol), "d" (port->regs.devcontrol)
    );
    
    wait400NS (port->regs.altstatus);
    restore_interrupts (flags);

}

static int ata_perform_read (struct ata_port_info *port, int lba48, size_t count, void *scratch) {

    uint16_t *buf16 = scratch;
    
    uint8_t status;
    size_t i;
    
    uint32_t previous;
    int copy;
    
    uintptr_t flags = save_interrupts ();
    __asm__ __volatile__ ("cli");
    
    previous = ata_irq_counter[port->irq - 14];
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (lba48 ? 0x24 : 0x20), "d" (port->regs.cmd)
    );
    
    for (i = 0; i < count; i++) {
    
        __asm__ __volatile__ ("sti");
        
        if (!wait_irq (port, previous, 0x01 | 0x20, &status)) {
        
            if (status == 0xFF) {
            
                dprintf ("[ata_perform_read] no IRQ or status FF at sector %lu\n", (unsigned long) i);
                
                restore_interrupts (flags);
                return 2;
            
            }
            
            dprintf ("[ata_perform_read] IRQ error at sector %lu status=%02x\n", (unsigned long) i, (unsigned) status);
            
            restore_interrupts (flags);
            return 3;
        
        }
        
        previous = ata_irq_counter[port->irq - 14];
        __asm__ __volatile__ ("cli");
        
        if (!poll (port, 0x01 | 0x20, 0x08, &status)) {
        
            dprintf ("[ata_perform_read] DRQ wait failed at sector %lu status=%02x\n", (unsigned long) i, (unsigned) status);
            
            restore_interrupts (flags);
            return 3;
        
        }
        
        copy = 256;
        
        __asm__ __volatile__ (
            "rep insw"
            : "+D" (buf16), "+c" (copy)
            : "d" (port->regs.data)
        );
    
    }
    
    restore_interrupts (flags);
    return 0;

}

int ata_identify (int channel, uint16_t *output) {

    struct ata_regs regs;
    
    uint8_t lbamid, lbahi, status;
    int count = 256, type = 2;
    
    uintptr_t flags = save_interrupts ();
    int slave, i;
    
    __asm__ __volatile__ ("cli");
    
    if (!(ata_get_reg_ports (channel, &regs) && ata_is_slave (channel, &slave))) {
    
        restore_interrupts (flags);
        return 0;
    
    }
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (slave ? 0xB0 : 0xA0), "d" (regs.drive)
    );
    
    wait400NS (regs.status);
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "d" (regs.status)
    );
    
    if (status == 0xFF) {
    
        restore_interrupts (flags);
        return 1;
    
    }
    
    if (!wait_busy (&regs)) {
    
        restore_interrupts (flags);
        return 0;
    
    }
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (0), "d" (regs.sectors)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (0), "d" (regs.lbalo)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (0), "d" (regs.lbamid)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (0), "d" (regs.lbahi)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (0xEC), "d" (regs.cmd)
    );
    
    wait400NS (regs.status);
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "d" (regs.status)
    );
    
    if (status == 0) {
    
        restore_interrupts (flags);
        return 1;
    
    }
    
    if (!wait_busy (&regs)) {
    
        restore_interrupts (flags);
        return 0;
    
    }
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (lbamid)
        : "d" (regs.lbamid)
    );
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (lbahi)
        : "d" (regs.lbahi)
    );
    
    if ((lbamid == 0x14 && lbahi == 0xEB) || (lbamid == 0x69 && lbahi == 0x96)) {
    
        type = 3;
        
        __asm__ __volatile__ (
            "outb %0, %1"
            :
            : "a" (0xA1), "d" (regs.cmd)
        );
        
        wait400NS (regs.status);
        
        if (!wait_busy (&regs)) {
        
            restore_interrupts (flags);
            return 0;
        
        }
        
    } else if (lbamid || lbahi) {
    
        restore_interrupts (flags);
        return 0;
    
    }
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "d" (regs.status)
    );
    
    for (i = 0; i < 3000; i++) {
    
        if (status & 0x21) {
        
            restore_interrupts (flags);
            return 0;
        
        }
        
        if (status & 0x08) {
        
            restore_interrupts (flags);
            break;
        
        }
        
        __asm__ __volatile__ (
            "inb %1, %0"
            : "=a" (lbamid)
            : "d" (regs.lbamid)
        );
        
        __asm__ __volatile__ (
            "inb %1, %0"
            : "=a" (lbahi)
            : "d" (regs.lbahi)
        );
        
        if (type == 2 && (lbamid || lbahi)) {
        
            restore_interrupts (flags);
            return 0;
        
        }
        
        __asm__ __volatile__ ("sti");
        
        thread_sleep (10);
        __asm__ __volatile__ ("cli");
        
        __asm__ __volatile__ (
            "inb %1, %0"
            : "=a" (status)
            : "d" (regs.status)
        );
    
    }
    
    if (i == 3000) {
    
        restore_interrupts (flags);
        return 0;
    
    }
    
    __asm__ __volatile__ (
        "rep insw"
        : "+D" (output), "+c" (count)
        : "d" (regs.data)
    );
    
    restore_interrupts (flags);
    return type;

}

static int ata_perform_write (struct ata_port_info *port, size_t count, void *scratch) {

    uint16_t *buf16 = scratch;
    uint8_t status;
    
    size_t i, j;
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (0x30), "d" (port->regs.cmd)
    );
    
    wait400NS (port->regs.altstatus);
    
    for (i = 0; i < count; i++) {
    
        if (!poll (port, 0x01 | 0x20, 0x08, &status)) {
            return 3;
        }
        
        for (j = 0; j < 256; j++) {
        
            __asm__ __volatile__ (
                "outw %0, %1"
                :
                : "a" (*buf16), "d" (port->regs.data)
            );
            
            buf16++;
        
        }
        
        wait400NS (port->regs.altstatus);
    
    }
    
    if (!wait_busy (&port->regs)) {
        return 3;
    }
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "d" (port->regs.status)
    );
    
    if (status & (0x01 | 0x20 | 0x08)) {
        return 3;
    }
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (0xE7), "d" (port->regs.cmd)
    );
    
    if (!wait_busy (&port->regs)) {
        return 3;
    }
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "d" (port->regs.status)
    );
    
    return (status & (0x01 | 0x20 | 0x08)) ? 3 : 0;

}

int ata_accesss_sectors (uint64_t sector, size_t count, void *scratch, struct ata_port_info *port, int access) {

    int use_lba48 = (sector > 0x0FFFFFFF || count > 0xFF);
    
    uint8_t stat = 0;
    int ret;
    
    if (use_lba48 || !count || access < 0 || access > 1) {
        return 1;
    }
    
    select_drive (port, use_lba48, sector);
    
    if (!poll (port, 0x01 | 0x20, 0x40, &stat)) {
        return 2;
    }
    
    setup_lba (port, use_lba48, sector, count);
    
    if (access) {
    
        if (count - 1 > 0x0FFFFFFFULL - sector) {
            return 1;
        }
        
        if ((ret = ata_perform_write (port, count, scratch))) {
            ata_soft_reset (port);
        }
        
        return ret;
    
    }
    
    if ((ret = ata_perform_read (port, use_lba48, count, scratch))) {
    
        dprintf ("[ata_accesss_sectors] read error ret=%d\n", ret);
        ata_soft_reset (port);
    
    }
    
    return ret;

}

int ata_get_reg_ports (uint16_t channel, struct ata_regs *regs) {

    if (!regs) {
        return 0;
    }
    
    switch (channel) {
    
        case 0:     case 1:
        
            regs->base = 0x01F0;
            regs->devcontrol = regs->altstatus = 0x03F6;
            
            break;
        
        case 2:     case 3:
        
            regs->base = 0x0170;
            regs->devcontrol = regs->altstatus = 0x0376;
            
            break;
        
        default:
        
            return 0;
    
    }
    
    regs->data      = regs->base + ATA_REG_DATA;
    regs->feature   = regs->base + ATA_REG_FEATURE;
    regs->error     = regs->base + ATA_REG_ERROR_INFO;
    regs->sectors   = regs->base + ATA_REG_SECTOR_COUNT;
    regs->lbalo     = regs->base + ATA_REG_LBALO;
    regs->lbamid    = regs->base + ATA_REG_LBAMID;
    regs->lbahi     = regs->base + ATA_REG_LBAHI;
    regs->drive     = regs->base + ATA_REG_DRIVE;
    regs->status    = regs->base + ATA_REG_STATUS;
    regs->cmd       = regs->base + ATA_REG_CMD;
    
    return 1;

}

int ata_get_irq (uint16_t channel, int *irq) {

    if (!irq) {
        return 0;
    }
    
    switch (channel) {
    
        case 0:     case 1:
        
            *irq = 14;
            break;
        
        case 2:     case 3:
        
            *irq = 15;
            break;
        
        default:
        
            return 0;
    
    }
    
    return 1;

}

int ata_is_slave (uint16_t channel, int *slave) {

    if (!slave || channel > 3) {
        return 0;
    }
    
    *slave = (channel == 1 || channel == 3);
    return 1;

}

void ata_install (void) {

    uintptr_t flags = save_interrupts ();
    __asm__ __volatile__ ("cli");
    
    if (!ata_irq_installed) {
    
        irq_install_handler (14, ata_irq_handler_primary, "ata-primary");
        irq_install_handler (15, ata_irq_handler_secondary, "ata-secondary");
        
#if     defined (__AMD64__)
        ata_ioapic_routed[0] = acpi_route_legacy_irq (14);
        ata_ioapic_routed[1] = acpi_route_legacy_irq (15);
#endif
        
        if (!ata_ioapic_routed[0]) {
            ata_unmask_pic_irq (14);
        }
        
        if (!ata_ioapic_routed[1]) {
            ata_unmask_pic_irq (15);
        }
        
        ata_irq_installed = 1;
    
    }
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (0), "d" (0x03F6)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (0), "d" (0x0376)
    );
    
    restore_interrupts (flags);

}

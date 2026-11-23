/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <corazon/hid/kbd.h>
#include    <corazon/tasking/process.h>
#include    <corazon/utils/misc.h>
#include    <corazon/vfs/vfs.h>
#include    <corazon/video/ttf.h>
#include    <corazon/acpi.h>
#include    <corazon/console.h>
#include    <corazon/cpu.h>
#include    <corazon/mmu.h>
#include    <corazon/timer.h>

#include    <fcntl.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>

__attribute__ ((packed)) struct mem_zone {

    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;

};

__attribute__ ((packed)) typedef struct mod_info {

    uintptr_t start_address;
    size_t length;

} mod_info_t;

__attribute__ ((packed)) struct boot_info {

    uint64_t boot_checksum;
    
    console_info_t console;
    void *acpi_rsdp;
    
    uint32_t mmap_size;
    void *mmap_addr;
    
    uint32_t mem_upper;
    
    mod_info_t *mod_info;
    uint64_t no_mods;

};

static struct thread *splash_thread = 0;
extern void syscalls_register (void);

extern int floppy_allocate_dma_buffer (void);
extern int init_floppies (uint64_t boot_checksum);

extern void hdd_install (uint64_t boot_hash, int disks);
extern void ata_install (void);

extern void console_install (void);
extern void zero_install (void);
extern void procfs_install (void);

extern int kopen (const char *__file, int __oflag);

static uintptr_t save_interrupts (void) {
    
    uintptr_t flags;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("pushfq");
#else
    __asm__ __volatile__ ("pushfd");
#endif
    
    __asm__ __volatile__ (
        "pop %0"
        : "=r" (flags)
        :
        : "memory"
    );
    
    return flags;

}

static void restore_interrupts (uintptr_t flags) {
    
    __asm__ __volatile__ (
        "push %0"
        :
        : "r" (flags)
        : "memory"
    );

#if     defined (__AMD64__)
    __asm__ __volatile__ ("popfq");
#else
    __asm__ __volatile__ ("popfd");
#endif

}

static void splash (void) {

    static const uint8_t bars[16][3] = {
    
        {   0,      170,        170     },
        {   170,    0,          170     },
        {   170,    170,        170     }
    
    };
    
    uint32_t width, height, bar_length = 48, logo_length = 58;
    uint32_t x, y, i = 0, j = 0;
    
    uintptr_t flags;
    
    width = console_get_width ();
    height = console_get_height ();
    
    y = ((height / 2) - 4);
    x = ((width / 2) - (logo_length / 2));
    
    console_move_cursor (x, y);
    kprintf ("  ____ _     _                                 ___  ____  \n");
    
    console_move_cursor (x, y + 1);
    kprintf (" / ___| |__ (_)_ __ ___   __ _  ___ _ __ __ _ / _ \\/ ___| \n");
    
    console_move_cursor (x, y + 2);
    kprintf ("| |   | '_ \\| | '_ ` _ \\ / _` |/ _ \\ '__/ _` | | | |___ \\ \n");
    
    console_move_cursor (x, y + 3);
    kprintf ("| |___| | | | | | | | | | (_| |  __/ | | (_| | |_|  ___) |\n");
    
    console_move_cursor (x, y + 4);
    kprintf (" \\____|_| |_|_|_| |_| |_|\\__,_|\\___|_|  \\__,_|\\___/|____/ \n");
    
    y = ((height / 2) + (height / 4));
    x = ((width / 2) - (bar_length / 2));
    
    console_move_cursor (x, y);
    
    for (;;) {
    
        if (i >= bar_length) {
        
            console_setbackgound (0, 0, 0);
            console_move_cursor (x, y);
            
            while (i > 0) {
            
                kprintf (" ");
                i--;
                
                flags = save_interrupts ();
                __asm__ __volatile__ ("sti");
                
                thread_sleep (20);
                restore_interrupts (flags);
            
            }
            
            console_move_cursor (x, y);
            continue;
        
        }
        
        if ((i % 12) == 0) {
        
            uint8_t r, g, b;
            
            r = bars[j][0];
            g = bars[j][1];
            b = bars[j][2];
            
            console_setbackgound (r, g, b);
            
            if (++j == 3) {
                j = 0;
            }
        
        }
        
        kprintf (" ");
        i++;
        
        flags = save_interrupts ();
        __asm__ __volatile__ ("sti");
        
        thread_sleep (20);
        restore_interrupts (flags);
        
    
    }

}

void kmain (struct boot_info *info) {

    struct boot_info info_copy = { 0 };
    struct mem_zone *memmap = 0;
    
    uint64_t reserved_mem = 0, physical_mem = 0, minimum_mem = 0;
    
    int disks = 0;
    uint64_t i;
    
    mod_info_t *mod_info;
    
    memcpy (&info_copy, info, sizeof (info_copy));
    memmap = (struct mem_zone *) info_copy.mmap_addr;
    
    /* Initialize the GDT. */
    gdt_install ();
    
    /* Initialize the IDT. */
    idt_install ();
    
    /* CPU. */
    cpu_install ();
    
    /* FPU/SSE magic */
    fpu_install ();
    
#if     defined (__AMD64__)
    /* PAT. */
    pat_install ();
#endif
    
    /* Initialize the PIC. */
    pic_initialize ();
    
    /* Initialize an early console. */
    console_initialize (&info_copy.console);
    
    /* Make sure there's enough memory. */
#if     defined (__AMD64__)
    physical_mem = (uint64_t) info_copy.mem_upper * 1024;
    
    minimum_mem  =   64;
    minimum_mem *= 1024;
    minimum_mem *= 1024;
    
    for (i = 0; i < info_copy.mmap_size; i++) {
    
        if (memmap[i].type == 2 && memmap[i].base == physical_mem) {
        
            uint64_t end = memmap[i].base + memmap[i].length;
            
            if (reserved_mem < end) {
                reserved_mem = end;
            }
        
        }
    
    }
    
    if (reserved_mem > 0) {
        physical_mem = reserved_mem;
    }
    
    if (physical_mem < minimum_mem) {
    
        kprintf ("panic: Corazon requires at least 16 MiB of memory\n");
        
        for (;;) {
        
#if     defined (__AMD64__)
            __asm__ __volatile__ ("pause");
#else
            __asm__ __volatile__ ("hlt");
#endif
        
        }
    
    }
    
    /* Initialze memory. */
    mmu_initialize (info_copy.mem_upper);
#else
    physical_mem = (uint64_t) info_copy.mem_upper * 1024;
    
    minimum_mem  =   16;
    minimum_mem *= 1024;
    minimum_mem *= 1024;
    
    for (i = 0; i < info_copy.mmap_size; i++) {
    
        if (memmap[i].type == 2 && memmap[i].base == physical_mem) {
        
            uint64_t end = memmap[i].base + memmap[i].length;
            
            if (reserved_mem < end) {
                reserved_mem = end;
            }
        
        }
    
    }
    
    if (reserved_mem > 0) {
        physical_mem = reserved_mem;
    }
    
    if (physical_mem < minimum_mem) {
    
        kprintf ("panic: Corazon requires at least 8 MiB of memory\n");
        
        for (;;) {
        
#if     defined (__AMD64__)
            __asm__ __volatile__ ("pause");
#else
            __asm__ __volatile__ ("hlt");
#endif
        
        }
    
    }
    
    /* Initialze memory. */
    {
    
        uintptr_t reserved_end = 0;
        size_t length;
        
        for (i = 0; i < info_copy.no_mods; i++) {
        
            mod_info = &info_copy.mod_info[i];
            
            length = mod_info->length;
            length = (length + 0xFFF) & ~0xFFFULL;
            
            if (reserved_end < mod_info->start_address + length) {
                reserved_end = mod_info->start_address + length;
            }
        
        }
        
        mmu_initialize (info_copy.mem_upper, reserved_end);
    
    }
#endif
    
    for (i = 0; i < info_copy.mmap_size; i++) {
    
        if (memmap[i].type == 1) {
            
            uint64_t base = memmap[i].base, length = memmap[i].length;
            mmu_mark_region_unused (base, length);
        
        }
    
    }
    
    for (i = 0; i < info_copy.no_mods; i++) {
    
        size_t length;
        mod_info = &info_copy.mod_info[i];
        
        length = mod_info->length;
        length = (length + 0xFFF) & ~0xFFFULL;
        
        mmu_mark_region_used (mod_info->start_address, length);
    
    }
    
    /* EFI passes rsdp so check for that. */
    if (info_copy.acpi_rsdp) {
        acpi_set_rsdp (info_copy.acpi_rsdp);
    }
    
    /* Try to initialize the com ports. */
    rs232_port_init ();
    
    /* Setup int 0x80 as the main interrupt. */
    syscalls_register ();
    
    /* Setup the timer. */
    timer_install (1000);
    
    /* Start multitasking. */
    tasking_initialize ();
    
    /* If ACPI is available then initialize it. */
    if (acpi_available ()) {
        acpi_install ();
    }
    
    kbd_install ();
    
    /* Enable interrupts. */
    __asm__ __volatile__ ("sti");
    
    /* Hide the cursor. */
    console_get_current ()->cursor_enabled = 0;
    
    /* Initialize a thread for the splash screen. */
    dprintf ("[splash] starting\n");
    splash_thread = thread_create_kthread (splash);
    
    /* Initialize the virtual filesystem. */
    dprintf ("[vfs] before install\n");
    vfs_install ();
    
    /* Map a virtual /dev directory. */
    dprintf ("[vfs] before map\n");
    map_vfs_directory ("/dev");
    
    /* Map a virtual /proc directory. */
    map_vfs_directory ("/proc");
    
    /* Initialize the /proc directory. */
    procfs_install ();
    
    /* Get the floppies. */
    dprintf ("[floppy] before init\n");
    disks += init_floppies (info_copy.boot_checksum);
    
    /* Intiialize the ATA controller. */
    dprintf ("[ata] before install\n");
    ata_install ();
    
    /* Find any ATA hard disks installed in the machine. */
    dprintf ("[hdd] before install\n");
    hdd_install (info_copy.boot_checksum, disks);
    
    if (!vfs_root_registered ()) {
    
        dprintf ("panic: no root drive present\n");
        
        for (;;) {
        
#if     defined (__AMD64__)
            __asm__ __volatile__ ("pause");
#else
            __asm__ __volatile__ ("hlt");
#endif
        
        }
    
    }
    
    /* Create /dev/console. */
    dprintf ("[console] before install\n");
    console_install ();
    
    /* Create /dev/null and /dev/zero. */
    dprintf ("[zero] before install\n");
    zero_install ();
    
    /* Open file descriptor 0, 1, and 2 as /dev/console. */
    dprintf ("[console] before open\n");
    
    kopen ("/dev/console", O_RDONLY);
    kopen ("/dev/console", O_RDONLY);
    kopen ("/dev/console", O_RDONLY);
    
    {
    
        uintptr_t flags = save_interrupts ();
        __asm__ __volatile__ ("cli");
        
        dprintf ("[modules] before exec\n");
        
        for (i = 0; i < info_copy.no_mods; i++) {
        
            mod_info = &info_copy.mod_info[i];
            
            if (process_create_user_task ((const void *) mod_info->start_address, mod_info->length, mod_info->start_address) < 0) {
                dprintf ("[fork] failed to start module\n");
            }
        
        }
        
        dprintf ("[splash] before removal\n");
        
        if (splash_thread && thread_destroy_kthread (splash_thread) < 0) {
        
            dprintf ("panic: failed to stop splash thread\n");
            
            for (;;) {
            
#if     defined (__AMD64__)
                __asm__ __volatile__ ("pause");
#else
                __asm__ __volatile__ ("hlt");
#endif
            
            }
        
        }
        
        dprintf ("[splash] after removal\n");
        splash_thread = 0;
        
        restore_interrupts (flags);
        
        console_setbackgound (0, 0, 0);
        console_setforegound (170, 170, 170);
        
        console_clear ();
        
#if     0
        if (console_get_type () != CONSOLE_TYPE_CGA){
        
            const char *path = "/usr/share/fonts/truetype/SpaceMono-Bold.ttf";
            struct fs_node *node = vfs_open (path);
            
            if (node) {
            
                off_t length = vfs_seek (node, 0, SEEK_END);
                
                if (length > 0 && length <= 4 * 1024 * 1024 && vfs_seek (node, 0, SEEK_SET) == 0) {
                
                    uint8_t *data = kmalloc ((size_t) length);
                    
                    if (data) {
                    
                        size_t read = 0;
                        
                        while (read < (size_t) length) {
                        
                            int ret = vfs_read (node, data + read, (size_t) length - read);
                            
                            if (ret <= 0) {
                                break;
                            }
                            
                            read += ret;
                        
                        }
                        
                        if (read == (size_t) length && console_font_load_ttf (data, (size_t) length) == 0) {
                            dprintf ("[console] loaded %s\n", path);
                        }
                        
                        kfree (data);
                    
                    }
                
                }
                
                vfs_close (node);
            
            }
        
        }
#endif
        
        console_get_current ()->cursor_enabled = 1;
        
        if (process_create_user_process_from_file ("/sbin/init") < 0) {
            kprintf ("[fork-test] failed to start init\n");
        }
    
    }
    
    for (;;) {
    
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("hlt");
#endif
    
    }

}

/******************************************************************************
 * @file            boot.c
 *****************************************************************************/
#include    <kaishi/pc/bios.h>
#include    <kaishi/vfs/node.h>
#include    <kaishi/vfs/vfs.h>
#include    <kaishi/axe.h>
#include    <kaishi/console.h>
#include    <kaishi/menu.h>
#include    <kaishi/misc.h>
#include    <kaishi/stdio.h>
#include    <kaishi/vector.h>

#include    <inttypes.h>
#include    <stdint.h>
#include    <string.h>

#define     VBE_MODE_SUPPORTED          0x0001
#define     VBE_SUCCESS                 0x004F
#define     VBE_MODE_END                0xFFFF

#define     VBE_MODE_LINEAR             0x0080
#define     VBE_SET_LINEAR              0x4000

__attribute__ ((packed)) struct mem_zone {

    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;

};

extern __attribute__ ((packed)) struct {

    uint32_t count;
    uint32_t reserved[3];
    struct mem_zone zones[256];

} mem_layout;

#define     CONSOLE_TYPE_TEXTMODE       0

#define     CONSOLE_TYPE_CGA            1
#define     CONSOLE_TYPE_EGA            2

#define     CONSOLE_TYPE_FRAMEBUFFER    3

__attribute__ ((packed)) typedef struct cga_info {

    uintptr_t address;
    
    uint32_t width;
    uint32_t height;

} cga_info_t;

__attribute__ ((packed)) typedef struct ega_info {

    uintptr_t address;
    
    uint32_t width;
    uint32_t height;

} ega_info_t;

__attribute__ ((packed)) typedef struct framebuffer_info {

    uint64_t address;
    uint64_t size;

    uint32_t width;
    uint32_t height;
    uint32_t pitch;

    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;

} framebuffer_info_t;

__attribute__ ((packed)) typedef struct textmode_info {

    uintptr_t address;
    
    uint32_t width;
    uint32_t height;

} textmode_info_t;

__attribute__ ((packed)) typedef struct {

    char     signature[4];
    uint16_t version;
    
    uint16_t oem_string_offset;
    uint16_t oem_string_segment;
    
    uint32_t capabilities;
    
    uint16_t video_mode_offset;
    uint16_t video_mode_segment;
    
    uint16_t total_memory;
    uint8_t  reserved[492];

} vbe_info_block_t;

__attribute__ ((packed)) typedef struct {

    uint16_t mode_attributes;
    uint8_t  win_a_attributes;
    uint8_t  win_b_attributes;
    uint16_t win_granularity;
    uint16_t win_size;
    uint16_t win_a_segment;
    uint16_t win_b_segment;
    uint32_t win_func_ptr;
    uint16_t bytes_per_scanline;
    
    uint16_t x_resolution;
    uint16_t y_resolution;
    uint8_t  x_char_size;
    uint8_t  y_char_size;
    uint8_t  number_of_planes;
    uint8_t  bits_per_pixel;
    uint8_t  number_of_banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  number_of_image_pages;
    uint8_t  reserved0;
    
    uint8_t  red_mask_size;
    uint8_t  red_field_position;
    uint8_t  green_mask_size;
    uint8_t  green_field_position;
    uint8_t  blue_mask_size;
    uint8_t  blue_field_position;
    uint8_t  reserved_mask_size;
    uint8_t  reserved_field_position;
    uint8_t  direct_color_mode_info;
    
    uint32_t phys_base_ptr;
    uint32_t off_screen_mem_offset;
    uint16_t off_screen_mem_size;
    
    uint8_t  reserved1[206];

} vbe_mode_info_t;

__attribute__ ((packed)) typedef struct console_info {

    uint32_t type;
    
    union {
    
        textmode_info_t textmode;
        
        cga_info_t cga;
        ega_info_t ega;
        
        framebuffer_info_t framebuffer;
    
    };

} console_info_t;

__attribute__ ((packed)) typedef struct mod_info {

    uintptr_t start_address;
    size_t length;

} mod_info_t;

__attribute__ ((packed)) struct {

    uint64_t boot_checksum;
    
    console_info_t console;
    void *acpi_rsdp;
    
    uint32_t mmap_size;
    void *mmap_addr;
    
    uint32_t mem_upper;
    
    mod_info_t *mod_info;
    uint64_t no_mods;

} boot_info = { 0 };

size_t get_screen_height (void);
size_t get_screen_width (void);

static uintptr_t far_to_linear (uint16_t segment, uint16_t offset) {
    return ((uintptr_t) segment << 4) + offset;
}

static int vbe_get_info (vbe_info_block_t *info) {

    REGS regs = { 0 };
    memset (info, 0, sizeof(*info));
    
    /* Request VBE 2.0+ information. */
    info->signature[0] = 'V';
    info->signature[1] = 'B';
    info->signature[2] = 'E';
    info->signature[3] = '2';
    
    regs.w.es = (uint16_t) ((uintptr_t) info >> 4);
    regs.w.di = (uint16_t) ((uintptr_t) info & 0x0F);
    
    regs.w.ax = 0x4F00;
    int386( 0x10, &regs, &regs);
    
    if (regs.w.ax != VBE_SUCCESS) {
        return 0;
    }
    
    if (info->signature[0] != 'V' || info->signature[1] != 'E' || info->signature[2] != 'S' || info->signature[3] != 'A') {
        return 0;
    }
    
    return 1;

}

static int vbe_get_mode_info (uint16_t mode, vbe_mode_info_t *info) {

    REGS regs = { 0 };
    memset (info, 0, sizeof (*info));
    
    regs.w.es = (uint16_t) ((uintptr_t) info >> 4);
    regs.w.di = (uint16_t) ((uintptr_t) info & 0x0F);
    
    regs.w.ax = 0x4F01;
    regs.w.cx = mode;
    
    int386 (0x10, &regs, &regs);
    
    if (regs.w.ax != VBE_SUCCESS) {
        return 0;
    }
    
    return 1;

}

static int vbe_set_mode (uint16_t mode) {

    REGS regs = { 0 };
    
    /* Bit 14 requests the linear framebuffer. */
    regs.w.bx = mode | VBE_SET_LINEAR;
    
    regs.w.ax = 0x4F02;
    int386 (0x10, &regs, &regs);
    
    if (regs.w.ax != VBE_SUCCESS) {
        return 0;
    }
    
    return 1;

}

static int vbe_find_mode (vbe_info_block_t *vbe, uint16_t width, uint16_t height, uint8_t bpp, uint16_t *selected_mode, vbe_mode_info_t *selected_info) {

    static vbe_mode_info_t info = { 0 };
    uint16_t mode;
    
    uint16_t *mode_list = (uint16_t *) far_to_linear (vbe->video_mode_segment, vbe->video_mode_offset);
    
    while ((mode = *mode_list++) != VBE_MODE_END) {
        
        if (!vbe_get_mode_info (mode, &info)) {
            continue;
        }
        
        /* Mode must be supported by the hardware. */
        if (!(info.mode_attributes & VBE_MODE_SUPPORTED)) {
            continue;
        }
        
        /* We specifically want a linear framebuffer. */
        if (!(info.mode_attributes & VBE_MODE_LINEAR)) {
            continue;
        }
        
        if (info.x_resolution != width) {
            continue;
        }
        
        if (info.y_resolution != height) {
            continue;
        }
        
        if (info.bits_per_pixel != bpp) {
            continue;
        }
        
        /* phys_base_ptr must be valid for a linear framebuffer. */
        if (info.phys_base_ptr == 0) {
            continue;
        }
        
        *selected_mode = mode;
        *selected_info = info;
        
        return 1;
    
    }
    
    return 0;

}

int framebuffer_initialize_vbe (void) {

    static vbe_info_block_t vbe = { 0 };
    static vbe_mode_info_t mode_info = { 0 };
    
    uint16_t mode;
    
    /* Ask the BIOS for its VBE information. */
    if (!vbe_get_info (&vbe)) {
        return 0;
    }
    
    /* Find an 800x600, 32-bit linear framebuffer mode. */
    if (!vbe_find_mode (&vbe, 800, 600, 32, &mode, &mode_info)) {
        return 0;
    }
    
    /* Switch into the mode we found. */
    if (!vbe_set_mode (mode)) {
        return 0;
    }
    
    /* Hook VBE directly into the common framebuffer structure. */
    boot_info.console.type                      = CONSOLE_TYPE_FRAMEBUFFER;
    
    boot_info.console.framebuffer.address       = mode_info.phys_base_ptr;
    boot_info.console.framebuffer.size          = mode_info.bytes_per_scanline * mode_info.y_resolution;
    
    boot_info.console.framebuffer.width         = mode_info.x_resolution;
    boot_info.console.framebuffer.height        = mode_info.y_resolution;
    boot_info.console.framebuffer.pitch         = mode_info.bytes_per_scanline;
    
    boot_info.console.framebuffer.red_mask      = ((1U << mode_info.red_mask_size) - 1) << mode_info.red_field_position;
    boot_info.console.framebuffer.green_mask    = ((1U << mode_info.green_mask_size) - 1) << mode_info.green_field_position;
    boot_info.console.framebuffer.blue_mask     = ((1U << mode_info.blue_mask_size) - 1) << mode_info.blue_field_position;
    
    boot_info.console.framebuffer.reserved_mask = ((1U << mode_info.reserved_mask_size) - 1) << mode_info.reserved_field_position;
    
    return 1;

}

extern void *kmalloc (size_t size);
extern void kfree (void *ptr);

static int load_modules (struct vector vec_mods, uintptr_t start_addr) {

    struct fs_node *node = 0;
    
    struct axe_exec32 *exec;
    uint8_t header[32];
    
    uintptr_t addr = start_addr;
    void (*entry) (void);
    
    struct axe_relocation_info32 *reloc;
    uint64_t mod_size = 0, mods_size = 0;
    
    uint64_t relocs = 0, symbolnum, address;
    uint64_t bss_size, i;
    
    int mod_index;
    
    boot_info.no_mods = vec_mods.length;
    mods_size = vec_mods.length * sizeof (*boot_info.mod_info);
    
    for (i = 0; i < mem_layout.count; i++) {
    
        uint64_t start = mem_layout.zones[i].base;
        uint64_t end = start + mem_layout.zones[i].length;
        
        uint64_t type = mem_layout.zones[i].type;
        
        if (type != 0x01) {
            continue;
        }
        
        if (start <= addr && end >= addr + mods_size) {
            break;
        }
    
    }
    
    if (i == mem_layout.count) {
    
        kprintf ("error: not enough memory for modules\n");
        return -1;
    
    }
    
    boot_info.mod_info = (mod_info_t *) (uintptr_t) addr;
    memset (boot_info.mod_info, 0, mods_size);
    
    addr += mods_size;
    
    for (mod_index = 0; mod_index < vec_mods.length; mod_index++) {
    
        addr = (addr + 0xFFF) & ~0xFFFULL;
        
        if (!(node = kopen (vec_mods.data[mod_index]))) {
        
            kprintf ("error: failed to open '%s' for reading\n", vec_mods.data[mod_index]);
            return -1;
        
        }
        
        if (kread (node, header, 4) < 4) {
        
            kclose (node);
            
            kprintf ("error: header size mismatch\n");
            return -1;
        
        }
        
        if (header[0] != 'A' || header[1] != 'X' || header[2] != 'E') {
        
            kclose (node);
            
            kprintf ("error: %s is an unsupported format\n", vec_mods.data[mod_index]);
            return -1;
        
        }
        
        if (header[3] != 3) {
        
            kclose (node);
            
            kprintf ("error: %s is not a 32-bit executable\n", vec_mods.data[mod_index]);
            return -1;
        
        }
        
        exec = (struct axe_exec32 *) header;
        
        if (kread (node, header + 4, sizeof (header) - 4) < sizeof (header) - 4) {
        
            kclose (node);
            
            kprintf ("error: header size mismatch\n");
            return -1;
        
        }
        
        mod_size = array_to_int (exec->a_text, 4) + array_to_int (exec->a_data, 4) + array_to_int (exec->a_bss, 4);
        
        for (i = 0; i < mem_layout.count; i++) {
        
            uint64_t start = mem_layout.zones[i].base;
            uint64_t end = start + mem_layout.zones[i].length;
            
            uint64_t type = mem_layout.zones[i].type;
            
            if (type != 0x01) {
                continue;
            }
            
            if (start <= addr && end >= addr + mod_size) {
                break;
            }
        
        }
        
        if (i == mem_layout.count) {
        
            kclose (node);
            
            kprintf ("error: not enough memory for the kernel\n");
            return -1;
        
        }
        
        mod_size -= array_to_int (exec->a_bss, 4);
        
        if (kread (node, (void *) (uintptr_t) addr, mod_size) < mod_size) {
        
            kclose (node);
            
            kprintf ("error: failed to load the kernel\n");
            return -1;
        
        }
        
        entry = (void (*) (void)) (uintptr_t) addr;
        
        if ((bss_size = array_to_int (exec->a_bss, 4))) {
            memset ((uint8_t *) entry + mod_size, 0, bss_size);
        }
        
        if ((relocs = array_to_int (exec->a_trsize, 4) / sizeof (*reloc))) {
        
            void *temp;
            size_t size;
            
            uint64_t value;
            
            size = array_to_int (exec->a_trsize, 4);
            temp = kmalloc (size);
            
            if (kread (node, temp, size) < size) {
            
                kprintf ("error: failed to read relocations\n");
                return -1;
            
            }
            
            reloc = (struct axe_relocation_info32 *) temp;
            
            for (i = 0; i < relocs; i++) {
            
                address = array_to_int (reloc[i].r_address, sizeof (reloc[i].r_address));
                symbolnum = array_to_int (reloc[i].r_symbolnum, sizeof (reloc[i].r_symbolnum));
                
                size = (symbolnum >> 24) & 0x06;
                
                if (size == 0x06) {
                
                    value = array_to_int ((uint8_t *) entry + address, 8);
                    int_to_array ((uint8_t *) entry + address, value + addr, 8);
                
                } else if (size == 0x04) {
                
                    value = array_to_int ((uint8_t *) entry + address, 4);
                    int_to_array ((uint8_t *) entry + address, value + addr, 4);
                
                } else if (size == 0x02) {
                
                    value = array_to_int ((uint8_t *) entry + address, 2);
                    int_to_array ((uint8_t *) entry + address, value + addr, 2);
                
                }
            
            }
            
            kfree (temp);
        
        }
        
        if ((relocs = array_to_int (exec->a_drsize, 4) / sizeof (*reloc))) {
        
            uint8_t *data = (uint8_t *) entry + array_to_int (exec->a_text, 4);
            
            void *temp;
            size_t size;
            
            uint64_t value;
            
            size = array_to_int (exec->a_drsize, 4);
            temp = kmalloc (size);
            
            if (kread (node, temp, size) < size) {
            
                kprintf ("error: failed to read relocations\n");
                return -1;
            
            }
            
            reloc = (struct axe_relocation_info32 *) temp;
            
            for (i = 0; i < relocs; i++) {
            
                address = array_to_int (reloc[i].r_address, sizeof (reloc[i].r_address));
                symbolnum = array_to_int (reloc[i].r_symbolnum, sizeof (reloc[i].r_symbolnum));
                
                size = (symbolnum >> 24) & 0x06;
                
                if (size == 0x06) {
                
                    value = array_to_int (data + address, 8);
                    int_to_array (data + address, value + addr, 8);
                
                } else if (size == 0x04) {
                
                    value = array_to_int (data + address, 4);
                    int_to_array (data + address, value + addr, 4);
                
                } else if (size == 0x02) {
                
                    value = array_to_int (data + address, 2);
                    int_to_array (data + address, value + addr, 2);
                
                }
            
            }
            
            kfree (temp);
        
        }
        
        kclose (node);
        
        boot_info.mod_info[mod_index].start_address = addr;
        boot_info.mod_info[mod_index].length = mod_size + bss_size;
        
        addr += boot_info.mod_info[mod_index].length;
    
    }
    
    return 0;

}

void boot (struct menu_entry *me) {

    struct fs_node *node = 0;
    uint64_t kernel_size = 0;
    
    struct axe_exec32 *exec;
    uint8_t header[32];
    
    uint64_t addr = 0x100000;
    void (*entry) (void);
    
    struct axe_relocation_info32 *reloc;
    
    uint64_t relocs = 0, symbolnum, address;
    uint64_t total_mem = 0, bss_size, i;
    
    if (!(node = kopen (me->kernel))) {
    
        kprintf ("error: failed to open '%s' for reading\n", me->kernel);
        return;
    
    }
    
    if (kread (node, header, 4) < 4) {
    
        kclose (node);
        
        kprintf ("error: header size mismatch\n");
        return;
    
    }
    
    if (header[0] != 'A' || header[1] != 'X' || header[2] != 'E') {
    
        kclose (node);
        
        kprintf ("error: %s is an unsupported format\n", me->kernel);
        return;
    
    }
    
    if (header[3] != 3) {
    
        kclose (node);
        
        kprintf ("error: %s is not a 32-bit executable\n", me->kernel);
        return;
    
    }
    
    exec = (struct axe_exec32 *) header;
    
    if (kread (node, header + 4, sizeof (header) - 4) < sizeof (header) - 4) {
    
        kclose (node);
        
        kprintf ("error: header size mismatch\n");
        return;
    
    }
    
    kernel_size = array_to_int (exec->a_text, 4) + array_to_int (exec->a_data, 4) + array_to_int (exec->a_bss, 4);
    
    for (i = 0; i < mem_layout.count; i++) {
    
        uint64_t start = mem_layout.zones[i].base;
        uint64_t end = start + mem_layout.zones[i].length;
        
        uint64_t type = mem_layout.zones[i].type;
        
        if (type != 0x01) {
            continue;
        }
        
        if (start <= addr && end >= addr + kernel_size) {
            break;
        }
    
    }
    
    if (i == mem_layout.count) {
    
        kclose (node);
        
        kprintf ("error: not enough memory for the kernel\n");
        return;
    
    }
    
    kernel_size -= array_to_int (exec->a_bss, 4);
    
    if (kread (node, (void *) (uintptr_t) addr, kernel_size) < kernel_size) {
    
        kclose (node);
        
        kprintf ("error: failed to load the kernel\n");
        return;
    
    }
    
    entry = (void (*) (void)) (uintptr_t) addr;
    
    if ((bss_size = array_to_int (exec->a_bss, 4))) {
        memset ((uint8_t *) entry + kernel_size, 0, bss_size);
    }
    
    if ((relocs = array_to_int (exec->a_trsize, 4) / sizeof (*reloc))) {
    
        void *temp;
        size_t size;
        
        uint64_t value;
        
        size = array_to_int (exec->a_trsize, 4);
        temp = kmalloc (size);
        
        if (kread (node, temp, size) < size) {
        
            kprintf ("error: failed to read relocations\n");
            return;
        
        }
        
        reloc = (struct axe_relocation_info32 *) temp;
        
        for (i = 0; i < relocs; i++) {
        
            address = array_to_int (reloc[i].r_address, sizeof (reloc[i].r_address));
            symbolnum = array_to_int (reloc[i].r_symbolnum, sizeof (reloc[i].r_symbolnum));
            
            size = (symbolnum >> 24) & 0x06;
            
            if (size == 0x06) {
            
                value = array_to_int ((uint8_t *) entry + address, 8);
                int_to_array ((uint8_t *) entry + address, value + addr, 8);
            
            } else if (size == 0x04) {
            
                value = array_to_int ((uint8_t *) entry + address, 4);
                int_to_array ((uint8_t *) entry + address, value + addr, 4);
            
            } else if (size == 0x02) {
            
                value = array_to_int ((uint8_t *) entry + address, 2);
                int_to_array ((uint8_t *) entry + address, value + addr, 2);
            
            }
        
        }
        
        kfree (temp);
    
    }
    
    if ((relocs = array_to_int (exec->a_drsize, 4) / sizeof (*reloc))) {
    
        uint8_t *data = (uint8_t *) entry + array_to_int (exec->a_text, 4);
        
        void *temp;
        size_t size;
        
        uint64_t value;
        
        size = array_to_int (exec->a_drsize, 4);
        temp = kmalloc (size);
        
        if (kread (node, temp, size) < size) {
        
            kprintf ("error: failed to read relocations\n");
            return;
        
        }
        
        reloc = (struct axe_relocation_info32 *) temp;
        
        for (i = 0; i < relocs; i++) {
        
            address = array_to_int (reloc[i].r_address, sizeof (reloc[i].r_address));
            symbolnum = array_to_int (reloc[i].r_symbolnum, sizeof (reloc[i].r_symbolnum));
            
            size = (symbolnum >> 24) & 0x06;
            
            if (size == 0x06) {
            
                value = array_to_int (data + address, 8);
                int_to_array (data + address, value + addr, 8);
            
            } else if (size == 0x04) {
            
                value = array_to_int (data + address, 4);
                int_to_array (data + address, value + addr, 4);
            
            } else if (size == 0x02) {
            
                value = array_to_int (data + address, 2);
                int_to_array (data + address, value + addr, 2);
            
            }
        
        }
        
        kfree (temp);
    
    }
    
    kclose (node);
    
    if (me->vec_modules.length > 0) {
    
        if (load_modules (me->vec_modules, addr + kernel_size + bss_size) < 0 ) {
            return;
        }
    
    }
    
    for (i = 0; i < mem_layout.count; i++) {
    
        if (mem_layout.zones[i].type == 1) {
        
            uint64_t end = mem_layout.zones[i].base + mem_layout.zones[i].length;
            
            if (total_mem < end) {
                total_mem = end;
            }
        
        }
    
    }
    
    console_hide_cursor ();
    console_clear ();
    
    boot_info.mem_upper = total_mem / 1024;
    
    boot_info.mmap_addr = (uint8_t *) &mem_layout + 16;
    boot_info.mmap_size = mem_layout.count;
    
    if (!framebuffer_initialize_vbe ()) {
    
        REGS r = { 0 };
            
        /* Test explicitly if an EGA or VGA subsystem actually exists */
        r.b.ah = 0x12;
        r.b.bl = 0x10;      /* Request EGA/VGA info */
        
        int386 (0x10, &r, &r);
        
        /* If BL is still 0x10, the BIOS/Hardware does NOT support EGA/VGA */
        if (r.b.bl != 0x10) {
            
            /* Hard EGA/VGA present! Safe to try Mode 0x0E */
            r.w.ax = 0x000E;
            int386 (0x10, &r, &r);
            
            r.b.ah = 0x0F;
            int386 (0x10, &r, &r);
            
            if ((r.b.al & 0x7F) == 0x0E) {
            
                boot_info.console.type = CONSOLE_TYPE_EGA;
                boot_info.console.ega.address = 0xA0000;
                boot_info.console.ega.width   = 640;
                boot_info.console.ega.height  = 200;
            
            }
            
        } else {
        
            /* Fallback safely because we are on a pure CGA / legacy text setup */
            r.w.ax = 0x0004;
            int386 (0x10, &r, &r);
            
            r.b.ah = 0x0F;
            int386 (0x10, &r, &r);
            
            if ((r.b.al & 0x7F) == 0x04) {
            
                boot_info.console.type = CONSOLE_TYPE_CGA;
                boot_info.console.cga.address = 0xB8000;
                boot_info.console.cga.width   = 320;
                boot_info.console.cga.height  = 200;
            
            } else {
            
                /* Force clean Text Mode 3 */
                r.w.ax = 0x0003;
                int386 (0x10, &r, &r);
                
                boot_info.console.type = CONSOLE_TYPE_TEXTMODE;
                boot_info.console.textmode.address = 0xB8000;
                boot_info.console.textmode.width   = get_screen_width ();
                boot_info.console.textmode.height  = get_screen_height ();
            
            }
        
        }
    
    }
    
    boot_info.boot_checksum = vfs_get_disk_checksum ();
    
    entry = (void (*) (void)) (uintptr_t) (addr + array_to_int (exec->a_entry, 4));
    __asm__ __volatile__ ("jmp %1" : : "b" (&boot_info), "c" (entry));

}

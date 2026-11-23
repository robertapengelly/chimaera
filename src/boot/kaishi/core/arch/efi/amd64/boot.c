/******************************************************************************
 * @file            boot.c
 *****************************************************************************/
#include    <kaishi/vfs/node.h>
#include    <kaishi/vfs/vfs.h>
#include    <kaishi/axe.h>
#include    <kaishi/console.h>
#include    <kaishi/efi.h>
#include    <kaishi/menu.h>
#include    <kaishi/misc.h>
#include    <kaishi/stdio.h>

#include    <inttypes.h>
#include    <stdint.h>
#include    <string.h>

__attribute__ ((packed)) struct mem_zone {

    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;

};

static EFI_GUID GraphicsOutputProtocol = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

static EFI_GUID Acpi20TableGuid        = EFI_ACPI_20_TABLE_GUID;
static EFI_GUID AcpiTableGuid          = EFI_ACPI_TABLE_GUID;

typedef struct {

    UINTN                 size;
    EFI_MEMORY_DESCRIPTOR *map;
    
    UINTN                 key;
    UINTN                 desc_size;
    
    UINT32                desc_version;

} Memory_Map_Info;

extern EFI_SYSTEM_TABLE *SystemTable;
extern EFI_BOOT_SERVICES *BootServices;

extern EFI_HANDLE *MainHandle;

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

static uint32_t memory_type (UINT32 type) {

    switch (type) {
    
        case EFI_CONVENTIONAL_MEMORY:
        
            return 1;       /* Usable */
        
        case EFI_ACPI_RECLAIM_MEMORY:
        
            return 3;       /* ACPI reclaimable */
        
        case EFI_ACPI_MEMORY_NVS:
        
            return 4;       /* ACPI NVS */
        
        case EFI_UNUSABLE_MEMORY:
        
            return 5;       /* Bad memory */
        
        default:
        
            return 2;       /* Reserved */
    
    }

}

EFI_STATUS get_memory_map (Memory_Map_Info *mmap) { 

    EFI_STATUS Status = EFI_SUCCESS;
    memset (mmap, 0, sizeof (*mmap));   /* Ensure input parm is initialized */
    
    /* Get initial memory map size (send 0 for map size) */
    Status = BootServices->GetMemoryMap (&mmap->size, mmap->map, &mmap->key, &mmap->desc_size, &mmap->desc_version);
    
    if (EFI_ERROR (Status) && Status != EFI_BUFFER_TOO_SMALL) {
    
        kprintf ("error: could not get initial memory map size\n");
        return Status;
    
    }
    
    /**
     * Allocate buffer for actual memory map for size in mmap->size;
     * 
     *   need to allocate enough space for an additional memory descriptor or 2 in
     *   the map due to this allocation itself.
     */
    mmap->size += mmap->desc_size * 2;
    
    if (EFI_ERROR (Status = BootServices->AllocatePool (EFI_LOADER_DATA, mmap->size, (VOID **) &mmap->map))) {
    
        kprintf ("error: could not allocate buffer for memory map\n");
        return Status;
    
    }
    
    /* Call get memory map again to get the actual memory map now that the buffer is the correct size */
    Status = BootServices->GetMemoryMap (&mmap->size, mmap->map, &mmap->key, &mmap->desc_size, &mmap->desc_version);
    
    if (EFI_ERROR (Status)) {
    
        kprintf ("error: could not get UEFI memory map! :(\n");
        return Status;
    
    }
    
    return EFI_SUCCESS;

}

extern void *kmalloc (size_t size);
extern void kfree (void *ptr);

static void load_modules (struct vector vec_mods) {

    struct fs_node *node = 0;
    
    struct axe_exec64 *exec;
    uint8_t header[64];
    
    EFI_PHYSICAL_ADDRESS addr = 0;
    void (*entry) (void);
    
    struct axe_relocation_info64 *reloc;
    uint64_t mod_size = 0, mods_size = 0;
    
    uint64_t relocs = 0, symbolnum, address;
    uint64_t bss_size, i;
    
    int mod_index;
    
    boot_info.no_mods = vec_mods.length;
    mods_size = vec_mods.length * sizeof (*boot_info.mod_info);
    
    if (EFI_ERROR (BootServices->AllocatePages (AllocateAnyPages, EFI_LOADER_DATA, (mods_size + 0xFFF) / 0x1000, &addr))) {
    
        kclose (node);
        
        kprintf ("error: not enough memory for modules\n");
        return;
    
    }
    
    boot_info.mod_info = (mod_info_t *) (uintptr_t) addr;
    
    for (mod_index = 0; mod_index < vec_mods.length; mod_index++) {
    
        addr = 0;
        
        if (!(node = kopen (vec_mods.data[mod_index]))) {
        
            kprintf ("error: failed to open '%s' for reading\n", vec_mods.data[mod_index]);
            continue;
        
        }
        
        if (kread (node, header, 4) < 4) {
        
            kclose (node);
            
            kprintf ("error: header size mismatch\n");
            continue;
        
        }
        
        if (header[0] != 'A' || header[1] != 'X' || header[2] != 'E') {
        
            kclose (node);
            
            kprintf ("error: %s is an unsupported format\n", vec_mods.data[mod_index]);
            continue;
        
        }
        
        if (header[3] != 62) {
        
            kclose (node);
            
            kprintf ("error: %s is not a 64-bit executable\n", vec_mods.data[mod_index]);
            continue;
        
        }
        
        exec = (struct axe_exec64 *) header;
        
        if (kread (node, header + 4, sizeof (header) - 4) < sizeof (header) - 4) {
        
            kclose (node);
            
            kprintf ("error: header size mismatch\n");
            continue;
        
        }
        
        mod_size = array_to_int (exec->a_text, 8) + array_to_int (exec->a_data, 8) + array_to_int (exec->a_bss, 8);
        
        if (EFI_ERROR (BootServices->AllocatePages (AllocateAnyPages, EFI_LOADER_DATA, (mod_size + 0xFFF) / 0x1000, &addr))) {
        
            kclose (node);
            
            kprintf ("error: not enough memory for module\n");
            return;
        
        }
        
        mod_size -= array_to_int (exec->a_bss, 8);
        
        if (kread (node, (void *) (uintptr_t) addr, mod_size) < mod_size) {
        
            kclose (node);
            
            kprintf ("error: failed to load the kernel\n");
            return;
        
        }
        
        entry = (void (*) (void)) (uintptr_t) addr;
        
        if ((bss_size = array_to_int (exec->a_bss, 8))) {
            memset ((uint8_t *) entry + mod_size, 0, bss_size);
        }
        
        if ((relocs = array_to_int (exec->a_trsize, 8) / sizeof (*reloc))) {
        
            void *temp;
            size_t size;
            
            uint64_t value;
            
            size = array_to_int (exec->a_trsize, 8);
            temp = kmalloc (size);
            
            if (kread (node, temp, size) < size) {
            
                kprintf ("error: failed to read relocations\n");
                return;
            
            }
            
            reloc = (struct axe_relocation_info64 *) temp;
            
            for (i = 0; i < relocs; i++) {
            
                address = array_to_int (reloc[i].r_address, sizeof (reloc[i].r_address));
                symbolnum = array_to_int (reloc[i].r_symbolnum, sizeof (reloc[i].r_symbolnum));
                
                size = (symbolnum >> 56) & 0x06;
                
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
        
        if ((relocs = array_to_int (exec->a_drsize, 8) / sizeof (*reloc))) {
        
            uint8_t *data = (uint8_t *) entry + array_to_int (exec->a_text, 8);
            
            void *temp;
            size_t size;
            
            uint64_t value;
            
            size = array_to_int (exec->a_drsize, 8);
            temp = kmalloc (size);
            
            if (kread (node, temp, size) < size) {
            
                kprintf ("error: failed to read relocations\n");
                return;
            
            }
            
            reloc = (struct axe_relocation_info64 *) temp;
            
            for (i = 0; i < relocs; i++) {
            
                address = array_to_int (reloc[i].r_address, sizeof (reloc[i].r_address));
                symbolnum = array_to_int (reloc[i].r_symbolnum, sizeof (reloc[i].r_symbolnum));
                
                size = (symbolnum >> 56) & 0x06;
                
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
    
    }

}

void boot (struct menu_entry *me) {

    struct fs_node *node = 0;
    uint64_t kernel_size = 0;
    
    struct axe_exec64 *exec;
    uint8_t header[64];
    
    EFI_PHYSICAL_ADDRESS addr = 0;
    void (*entry) (void);
    
    UINTN max_retries = 5, retries = 0;
    Memory_Map_Info mmap = { 0 };
    
    struct axe_relocation_info64 *reloc;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    
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
    
    if (header[3] != 62) {
    
        kclose (node);
        
        kprintf ("error: %s is not a 64-bit executable\n", me->kernel);
        return;
    
    }
    
    exec = (struct axe_exec64 *) header;
    
    if (kread (node, header + 4, sizeof (header) - 4) < sizeof (header) - 4) {
    
        kclose (node);
        
        kprintf ("error: header size mismatch\n");
        return;
    
    }
    
    kernel_size = array_to_int (exec->a_text, 8) + array_to_int (exec->a_data, 8) + array_to_int (exec->a_bss, 8);
    
    if (EFI_ERROR (BootServices->AllocatePages (AllocateAnyPages, EFI_LOADER_DATA, (kernel_size + 0xFFF) / 0x1000, &addr))) {
    
        kclose (node);
        
        kprintf ("error: not enough memory for the kernel\n");
        return;
    
    }
    
    kernel_size -= array_to_int (exec->a_bss, 8);
    
    if (kread (node, (void *) (uintptr_t) addr, kernel_size) < kernel_size) {
    
        kclose (node);
        
        kprintf ("error: failed to load the kernel\n");
        return;
    
    }
    
    entry = (void (*) (void)) (uintptr_t) addr;
    
    if ((bss_size = array_to_int (exec->a_bss, 8))) {
        memset ((uint8_t *) entry + kernel_size, 0, bss_size);
    }
    
    if ((relocs = array_to_int (exec->a_trsize, 8) / sizeof (*reloc))) {
    
        void *temp;
        size_t size;
        
        uint64_t value;
        
        size = array_to_int (exec->a_trsize, 8);
        temp = kmalloc (size);
        
        if (kread (node, temp, size) < size) {
        
            kprintf ("error: failed to read relocations\n");
            return;
        
        }
        
        reloc = (struct axe_relocation_info64 *) temp;
        
        for (i = 0; i < relocs; i++) {
        
            address = array_to_int (reloc[i].r_address, sizeof (reloc[i].r_address));
            symbolnum = array_to_int (reloc[i].r_symbolnum, sizeof (reloc[i].r_symbolnum));
            
            size = (symbolnum >> 56) & 0x06;
            
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
    
    if ((relocs = array_to_int (exec->a_drsize, 8) / sizeof (*reloc))) {
    
        uint8_t *data = (uint8_t *) entry + array_to_int (exec->a_text, 8);
        
        void *temp;
        size_t size;
        
        uint64_t value;
        
        size = array_to_int (exec->a_drsize, 8);
        temp = kmalloc (size);
        
        if (kread (node, temp, size) < size) {
        
            kprintf ("error: failed to read relocations\n");
            return;
        
        }
        
        reloc = (struct axe_relocation_info64 *) temp;
        
        for (i = 0; i < relocs; i++) {
        
            address = array_to_int (reloc[i].r_address, sizeof (reloc[i].r_address));
            symbolnum = array_to_int (reloc[i].r_symbolnum, sizeof (reloc[i].r_symbolnum));
            
            size = (symbolnum >> 56) & 0x06;
            
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
        load_modules (me->vec_modules);
    }
    
    console_hide_cursor ();
    console_clear ();
    
    if (EFI_ERROR (BootServices->LocateProtocol (&GraphicsOutputProtocol, NULL, (void **) &gop))) {
        return;
    }
    
    boot_info.console.type = CONSOLE_TYPE_FRAMEBUFFER;
    
    boot_info.console.framebuffer.address = (uint64_t) gop->Mode->FrameBufferBase;
    boot_info.console.framebuffer.size    = (uint64_t) gop->Mode->FrameBufferSize;
    
    boot_info.console.framebuffer.width   = gop->Mode->Info->HorizontalResolution;
    boot_info.console.framebuffer.height  = gop->Mode->Info->VerticalResolution;
    
    /* GOP framebuffer pixels are 32 bits for the normal RGB/BGR modes. */
    boot_info.console.framebuffer.pitch = gop->Mode->Info->PixelsPerScanLine * 4;
    
    switch (gop->Mode->Info->PixelFormat) {
    
        case PIXEL_RED_GREEN_BLUE_RESERVED_8BIT_PER_COLOR:
        
            boot_info.console.framebuffer.red_mask      = 0x000000FF;
            boot_info.console.framebuffer.green_mask    = 0x0000FF00;
            boot_info.console.framebuffer.blue_mask     = 0x00FF0000;
            boot_info.console.framebuffer.reserved_mask = 0xFF000000;
            
            break;
        
        case PIXEL_BLUE_GREEN_RED_RESERVED_8BIT_PER_COLOR:
        
            boot_info.console.framebuffer.red_mask      = 0x00FF0000;
            boot_info.console.framebuffer.green_mask    = 0x0000FF00;
            boot_info.console.framebuffer.blue_mask     = 0x000000FF;
            boot_info.console.framebuffer.reserved_mask = 0xFF000000;
            
            break;
        
        case PIXEL_BIT_MASK:
        
            boot_info.console.framebuffer.red_mask = gop->Mode->Info->PixelInformation.RedMask;
            boot_info.console.framebuffer.green_mask = gop->Mode->Info->PixelInformation.GreenMask;
            boot_info.console.framebuffer.blue_mask = gop->Mode->Info->PixelInformation.BlueMask;
            boot_info.console.framebuffer.reserved_mask = gop->Mode->Info->PixelInformation.ReservedMask;
            
            break;
        
        default:
        
            /* PixelBltOnly: no framebuffer that corazon can directly use. */
            boot_info.console.framebuffer.address = 0;
            boot_info.console.framebuffer.size = 0;
            
            break;
    
    }
    
    if (EFI_ERROR (get_memory_map (&mmap))) {
        return;
    }
    
    boot_info.acpi_rsdp = 0;
    
    for (i = 0; i < SystemTable->NumberOfTableEntries; i++) {
    
        EFI_CONFIGURATION_TABLE *table = &SystemTable->ConfigurationTable[i];

        if (!memcmp (&table->VendorGuid, &Acpi20TableGuid, sizeof (EFI_GUID)) || !memcmp (&table->VendorGuid, &AcpiTableGuid, sizeof (EFI_GUID))) {
        
            boot_info.acpi_rsdp = table->VendorTable;
            break;
        
        }
    
    }
    
    for (;;) {
    
        EFI_STATUS status = BootServices->ExitBootServices (MainHandle, mmap.key);
        
        if (!EFI_ERROR (status)) {
            break;
        }
        
        if (retries++ == max_retries) {
        
            kprintf ("error: could not Exit Boot Services!\r\n");
            return;
        
        }
        
        BootServices->FreePool (mmap.map);
        
        if (EFI_ERROR (get_memory_map (&mmap))) {
            return;
        }
    
    }
    
    {
    
        struct mem_zone *memmap = (struct mem_zone *) mmap.map;
        UINTN entries = mmap.size / mmap.desc_size;
        
        for (i = 0; i < entries; i++) {
        
            EFI_MEMORY_DESCRIPTOR *desc;
            
            uint64_t base, length, end;
            uint32_t type;
            
            desc = (EFI_MEMORY_DESCRIPTOR *) ((uint8_t *) mmap.map + (i * mmap.desc_size));
            
            /*
            * Save everything before writing the compacted entry, because
            * the output occupies the same buffer as the EFI descriptors.
            */
            base = desc->PhysicalStart;
            length = desc->NumberOfPages << 12;
            
            type = memory_type (desc->Type);
            
            memmap[i].base = base;
            memmap[i].length = length;
            memmap[i].type = type;
            memmap[i].attributes = 1;
            
            if (memmap[i].type == 1) {
            
                end = memmap[i].base + memmap[i].length;
                
                if (total_mem < end) {
                    total_mem = end;
                }
            
            }
        
        }
        
        boot_info.mmap_addr = memmap;
        boot_info.mmap_size = entries;
        
        boot_info.mem_upper = total_mem / 1024;
    
    }
    
    boot_info.boot_checksum = vfs_get_disk_checksum ();
    
    entry = (void (*) (void)) (uintptr_t) (addr + array_to_int (exec->a_entry, 8));
    __asm__ __volatile__ ("jmp %1" : : "b" (&boot_info), "c" (entry));

}

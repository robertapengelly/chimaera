/******************************************************************************
 * @file            mmu.c
 *****************************************************************************/
#include    <corazon/utils/misc.h>
#include    <corazon/mmu.h>

#include    <errno.h>
#include    <stddef.h>
#include    <stdint.h>
#include    <string.h>

/******************************************************************************
 * Paging constants.
 *****************************************************************************/
#define     PAGESIZE_4M                 0x00400000UL

#define     PG_PRESENT                  0x00000001UL
#define     PG_WRITE                    0x00000002UL
#define     PG_4MB                      0x00000080UL

#define     CR0_PG                      0x80000000UL
#define     CR4_PSE                     0x00000010UL

#define     PAGE_DIRECTORY_ENTRIES      1024
#define     PAGE_TABLE_ENTRIES          1024

/******************************************************************************
 * Physical memory information.
 *****************************************************************************/
static uint32_t total_page_count = 0;

/**
 * Allocate enough storage that we can manually align the page directory to
 * a 4 KiB boundary without depending on compiler-specific alignment support.
 */
static uint32_t page_directory_storage[2048];

static uint32_t *kernel_page_directory = 0;
uintptr_t kernel_page_directory_physical = 0;

static uintptr_t page_tables_start = 0;
static uintptr_t page_tables_end = 0;

extern int cpu_has_pse (void);

#define     PAGE_BITMAP_SIZE            (MAX_PAGE_COUNT / 32)
static uint32_t page_bitmap[PAGE_BITMAP_SIZE];

static uint32_t allocation_end_bitmap[PAGE_BITMAP_SIZE];

extern uintptr_t kernel_start;
extern uintptr_t kernel_end;

/******************************************************************************
 * @function        mmu_save_interrupts
 *****************************************************************************/
static uintptr_t mmu_save_interrupts (void) {

    uintptr_t flags;
    
    __asm__ __volatile__ ("pushfd");
    __asm__ __volatile__ ("pop %0" : "=r" (flags) : : "memory");
    
    __asm__ __volatile__ ("cli");
    return flags;

}

/******************************************************************************
 * @function        mmu_restore_interrupts
 *****************************************************************************/
static void mmu_restore_interrupts (uintptr_t flags) {

    __asm__ __volatile__ ("push %0" : : "r" (flags) : "memory");
    __asm__ __volatile__ ("popfd");

}

/******************************************************************************
 * @function        mmu_initialize
 *****************************************************************************/
void mmu_initialize (uint32_t high_mem, uintptr_t reserved_end) {

    uintptr_t directory, table;
    uint32_t cr0, cr4, i, j;
    
    int pse;
    
    /* Mark all memory as unavailable. */
    for (i = 0; i < PAGE_BITMAP_SIZE; i++) {
    
        page_bitmap[i] = 0xFFFFFFFFUL;
        allocation_end_bitmap[i] = 0;
    
    }
    
    /* high_mem is expressed in KiB. */
    total_page_count = (high_mem * 1024UL) / PAGE_SIZE;
    
    /* Find a 4 KiB-aligned page inside page_directory_storage. */
    directory = (uintptr_t) page_directory_storage;
    
    directory += PAGE_SIZE - 1;
    directory &= ~((uintptr_t) PAGE_SIZE - 1);
    
    kernel_page_directory = (uint32_t *) directory;
    
    /**
     * corazon is currently running identity mapped / without paging, so the
     * address of the page directory is also its physical address.
     */
    kernel_page_directory_physical = (uint32_t) (uintptr_t) kernel_page_directory;
    
    /* Check if the CPU has PSE. */
    pse = cpu_has_pse ();
    
    if (pse) {
    
        /**
         * Preserve the existing behaviour on CPUs supporting PSE:
         * identity-map the complete 32-bit address space using 4 MiB pages.
         */
        for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
            kernel_page_directory[i] = (i * PAGESIZE_4M) | PG_PRESENT | PG_WRITE | PG_4MB;
        }
        
        /* Enable Page Size Extensions. */
        __asm__ __volatile__ (
            "mov %0, cr4"
            : "=r" (cr4)
        );
        
        cr4 |= CR4_PSE;
        
        __asm__ __volatile__ (
            "mov cr4, %0"
            :
            : "r" (cr4)
        );
    
    } else {
    
        uint32_t table_count;
        
        /**
         * 386 fallback.
         *
         * A 386 has no CR4/PSE, so use normal 4 KiB pages. Allocate only
         * enough page tables to identity-map the detected physical memory.
         */
        
        for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
            kernel_page_directory[i] = 0;
        }
        
        table_count = (total_page_count + PAGE_TABLE_ENTRIES - 1) / PAGE_TABLE_ENTRIES;
        
        if (table_count > PAGE_DIRECTORY_ENTRIES) {
            table_count = PAGE_DIRECTORY_ENTRIES;
        }
        
        /**
         * Place the page tables immediately after the kernel image. They are
         * excluded from the free-page bitmap by mmu_mark_region_unused().
         */
        table = kernel_start + kernel_end;
        
        if (table < reserved_end) {
            table = reserved_end;
        }
        
        table += PAGE_SIZE - 1;
        table &= ~((uintptr_t) PAGE_SIZE - 1);
        
        page_tables_start = table;
        page_tables_end = table + ((uintptr_t) table_count * PAGE_SIZE);
        
        for (i = 0; i < table_count; i++) {
        
            uint32_t *pt = (uint32_t *) (table + ((uintptr_t) i * PAGE_SIZE));
            
            for (j = 0; j < PAGE_TABLE_ENTRIES; j++) {
            
                uint32_t page = (i * PAGE_TABLE_ENTRIES) + j;
                
                if (page < total_page_count) {
                    pt[j] = (page * PAGE_SIZE) | PG_PRESENT | PG_WRITE;
                } else {
                    pt[j] = 0;
                }
            
            }
            
            kernel_page_directory[i] = ((uint32_t) pt) | PG_PRESENT | PG_WRITE;
        
        }
    
    }
    
    /* Install the new page directory. */
    __asm__ __volatile__ (
        "mov cr3, %0"
        :
        : "r" (kernel_page_directory_physical)
    );
    
    /* Enable paging. */
    __asm__ __volatile__ (
        "mov %0, cr0"
        : "=r" (cr0)
    );
    
    cr0 |= CR0_PG;
    
    __asm__ __volatile__ (
        "mov cr0, %0"
        :
        : "r" (cr0)
    );

}

/******************************************************************************
 * @function        mmu_mark_page_used
 *****************************************************************************/
static void mmu_mark_page_used (uint32_t page) {
    page_bitmap[page >> 5] |= (1UL << (page & 31));
}

/******************************************************************************
 * @function        mmu_page_used
 *****************************************************************************/
static int mmu_page_used (uint32_t page) {
    return ((page_bitmap[page >> 5] & (1UL << (page & 31))) != 0);
}

/******************************************************************************
 * @function        mmu_mark_page_unused
 *****************************************************************************/
static void mmu_mark_page_unused (uint32_t page) {
    page_bitmap[page >> 5] &= ~(1UL << (page & 31));
}

/******************************************************************************
 * @function        mmu_mark_region_unused
 *****************************************************************************/
void mmu_mark_region_unused (uint64_t base, uint64_t length) {

    uintptr_t saved_flags;
    
    uint64_t start, end;
    uint64_t page;
    
    start = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    end = (base + length) & ~(PAGE_SIZE - 1);
    
    saved_flags = mmu_save_interrupts ();
    
    for (page = start / PAGE_SIZE; page < end / PAGE_SIZE && page < total_page_count; page++) {
    
        uint64_t address = page * PAGE_SIZE;
        
        /* Never release memory up to 0x500. */
        if (address < 0x500) {
            continue;
        }
        
        /* Keep 0x10000 – 0x1FFFF for the floppy dma buffer. */
        if (address >= 0x10000 && address <= 0x20000) {
            continue;
        }
        
        /* Never release the kernel image. */
        if (address >= kernel_start && address < kernel_start + kernel_end) {
            continue;
        }
        
        /* Never release the active 386 page tables. */
        if (address >= page_tables_start && address < page_tables_end) {
            continue;
        }
        
        mmu_mark_page_unused (page);
    
    }
    
    mmu_restore_interrupts (saved_flags);

}

/******************************************************************************
 * @function        mmu_mark_region_used
 *****************************************************************************/
void mmu_mark_region_used (uint64_t base, uint64_t length) {

    uintptr_t saved_flags;
    
    uint64_t start, end;
    uint64_t page;
    
    start = base & ~(PAGE_SIZE - 1);
    end = (base + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    saved_flags = mmu_save_interrupts ();
    
    for (page = start / PAGE_SIZE;page < end / PAGE_SIZE && page < total_page_count; page++) {
        mmu_mark_page_used ((uint32_t) page);
    
    }
    
    mmu_restore_interrupts (saved_flags);

}

/******************************************************************************
 * @function        mmu_get_memory_info
 *****************************************************************************/
void mmu_get_memory_info (uint64_t *total, uint64_t *free) {

    uintptr_t saved_flags;
    
    uint64_t free_pages = 0;
    uint32_t page;
    
    saved_flags = mmu_save_interrupts ();
    
    for (page = 0; page < total_page_count; page++) {
    
        if (!mmu_page_used (page)) {
            free_pages++;
        }
    
    }
    
    *total = (uint64_t) total_page_count * (PAGE_SIZE / 1024);
    *free = free_pages * (PAGE_SIZE / 1024);
    
    mmu_restore_interrupts (saved_flags);

}

#define     PG_PRESENT                  0x00000001UL
#define     PG_WRITE                    0x00000002UL
#define     PG_USER                     0x00000004UL
#define     PG_4MB                      0x00000080UL

#define     PG_ADDRESS                  0xFFFFF000UL
#define     PG_4MB_ADDRESS              0xFFC00000UL

/******************************************************************************
 * @function        mmu_current_page_directory
 *****************************************************************************/
static uint32_t *mmu_current_page_directory (void) {

    uintptr_t cr3;
    
    __asm__ __volatile__ ("mov %0, cr3" : "=r" (cr3));
    return (uint32_t *) (cr3 & PG_ADDRESS);

}

/******************************************************************************
 * @function        mmu_allocate_page
 *****************************************************************************/
static uintptr_t mmu_allocate_page (void) {

    uint32_t page;
    
    for (page = 1; page < total_page_count; page++) {
    
        if (!mmu_page_used (page)) {
        
            mmu_mark_page_used (page);
            return (uintptr_t) (page * PAGE_SIZE);
        
        }
    
    }
    
    return 0;

}

#define     MAP_PRIVATE                 0x02
#define     MAP_ANONYMOUS               0x20

/******************************************************************************
 * @function        mmu_split_page
 *****************************************************************************/
static uint32_t *mmu_split_page (uint32_t *pd, uint32_t index, int mmap_flags) {

    uint32_t *pt, i;
    
    uint32_t physical;
    uint32_t flags;
    
    uintptr_t page;
    
    if (!(pd[index] & PG_4MB)) {
        return (uint32_t *) (uintptr_t) (pd[index] & PG_ADDRESS);
    }
    
    page = mmu_allocate_page ();
    
    if (!page) {
        return 0;
    }
    
    pt = (uint32_t *) page;
    
    physical = pd[index] & PG_4MB_ADDRESS;
    flags = pd[index] & (PG_PRESENT | PG_WRITE | PG_USER);
    
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
    
        if (mmap_flags & MAP_ANONYMOUS) {
            pt[i] = 0;
        } else {
            pt[i] = physical | flags;
        }
        
        physical += PAGE_SIZE;
    
    }
    
    pd[index] = ((uint32_t) page) | flags;
    
    __asm__ __volatile__ (
        "mov cr3, %0"
        :
        : "r" ((uintptr_t) mmu_current_page_directory ())
    );
    
    return pt;

}

/******************************************************************************
 * @function        mmu_map_page
 *****************************************************************************/
static int mmu_map_page (uintptr_t virtual, uintptr_t physical, uint32_t flags, int mmap_flags) {

    uint32_t *directory = mmu_current_page_directory ();
    uint32_t *pt;
    
    uintptr_t page;
    uint32_t i;
    
    uint32_t pd_index, pt_index;
    
    pd_index = (uint32_t) (virtual >> 22);
    pt_index = (uint32_t) ((virtual >> 12) & 0x3FF);
    
    if (directory[pd_index] & PG_4MB) {
    
        pt = mmu_split_page (directory, pd_index, mmap_flags);
        
        if (!pt) {
            return 0;
        }
    
    } else if (!(directory[pd_index] & PG_PRESENT)) {
    
        page = mmu_allocate_page ();
        
        if (!page) {
            return 0;
        }
        
        pt = (uint32_t *) page;
        
        for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            pt[i] = 0;
        }
        
        directory[pd_index] = ((uint32_t) page) | PG_PRESENT | PG_WRITE | (flags & PG_USER);
    
    } else {
        pt = (uint32_t *) (uintptr_t) (directory[pd_index] & PG_ADDRESS);
    }
    
    if (flags & PG_USER) {
        directory[pd_index] |= PG_USER;
    }
    
    __asm__ __volatile__ (
        "mov cr3, %0"
        :
        : "r" ((uintptr_t) directory)
        : "memory"
    );
    
    pt[pt_index] = ((uint32_t) physical & PG_ADDRESS) | PG_PRESENT | flags;
    return 1;

}

/******************************************************************************
 * @function        mmu_install_user_page
 *****************************************************************************/
int mmu_install_user_page (uintptr_t pd, uintptr_t virtual, const void *contents, size_t size) {

    uintptr_t previous_pd, physical, saved_flags;
    int mapped = 0;
    
    if (!pd || !virtual || (virtual & (PAGE_SIZE - 1)) || size > PAGE_SIZE || pd == kernel_page_directory_physical) {
        return 0;
    }
    
    saved_flags = mmu_save_interrupts ();
    
    __asm__ __volatile__ ("mov %0, cr3" : "=r" (previous_pd));
    __asm__ __volatile__ ("mov cr3, %0" : : "r" (pd) : "memory");
    
    physical = mmu_allocate_page ();
    
    if (physical) {
    
        memset ((void *) physical, 0, PAGE_SIZE);
        
        if (contents && size) {
            memcpy ((void *) physical, contents, size);
        }
        
        if (mmu_map_page (virtual, physical, PG_USER | PG_WRITE, MAP_PRIVATE)) {
            mapped = 1;
        } else {
            mmu_mark_page_unused (physical / PAGE_SIZE);
        }
    
    }
    
    __asm__ __volatile__ ("mov cr3, %0" : : "r" (previous_pd) : "memory");
    mmu_restore_interrupts (saved_flags);
    
    return mapped;

}

/******************************************************************************
 * @function        mmu_destroy_address_space
 *****************************************************************************/
void mmu_destroy_address_space (uintptr_t pd) {

    uint32_t *directory = (uint32_t *) pd;
    
    uintptr_t saved_flags;
    uint32_t i, j;
    
    if (!pd || pd == kernel_page_directory_physical) {
        return;
    }
    
    saved_flags = mmu_save_interrupts ();
    
    for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
    
        uint32_t entry = directory[i];
        uint32_t *table;
        
        if (!(entry & PG_PRESENT) || (entry & PG_4MB)) {
            continue;
        }
        
        table = (uint32_t *) (uintptr_t) (entry & PG_ADDRESS);
        
        for (j = 0; j < PAGE_TABLE_ENTRIES; j++) {
        
            if ((table[j] & (PG_PRESENT | PG_USER)) == (PG_PRESENT | PG_USER)) {
                mmu_mark_page_unused ((table[j] & PG_ADDRESS) / PAGE_SIZE);
            }
        
        }
        
        mmu_mark_page_unused ((uintptr_t) table / PAGE_SIZE);
    
    }
    
    mmu_mark_page_unused (pd / PAGE_SIZE);
    mmu_restore_interrupts (saved_flags);

}

/******************************************************************************
 * @function        mmu_clone_address_space
 *****************************************************************************/
uintptr_t mmu_clone_address_space (uintptr_t parent_pd) {

    uint32_t *parent = (uint32_t *) parent_pd;
    uint32_t *child, *source, *target;
    
    uintptr_t saved_flags, page, table_page;
    uint32_t i, j;
    
    if (!parent_pd) {
        return 0;
    }
    
    saved_flags = mmu_save_interrupts ();
    page = mmu_allocate_page ();
    
    if (!page) {
        mmu_restore_interrupts (saved_flags);
        return 0;
    }
    
    child = (uint32_t *) page;
    
    for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        child[i] = 0;
    }
    
    for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
    
        if ((parent[i] & (PG_PRESENT | PG_USER | PG_4MB)) == (PG_PRESENT | PG_USER | PG_4MB)) {
            break;
        }
        
        if (!(parent[i] & PG_PRESENT) || (parent[i] & PG_4MB)) {
            child[i] = parent[i];
            continue;
        }
        
        table_page = mmu_allocate_page ();
        
        if (!table_page) {
            break;
        }
        
        source = (uint32_t *) (uintptr_t) (parent[i] & PG_ADDRESS);
        target = (uint32_t *) table_page;
        
        for (j = 0; j < PAGE_TABLE_ENTRIES; j++) {
            target[j] = 0;
        }
        
        child[i] = (uint32_t) table_page | (parent[i] & ~PG_ADDRESS);
        
        for (j = 0; j < PAGE_TABLE_ENTRIES; j++) {
        
            if ((source[j] & (PG_PRESENT | PG_USER)) == (PG_PRESENT | PG_USER)) {
            
                page = mmu_allocate_page ();
                
                if (!page) {
                    break;
                }
                
                memcpy ((void *) page, (void *) (uintptr_t) (source[j] & PG_ADDRESS), PAGE_SIZE);
                target[j] = (uint32_t) page | (source[j] & ~PG_ADDRESS);
            
            } else {
                target[j] = source[j];
            }
        
        }
        
        if (j != PAGE_TABLE_ENTRIES) {
            break;
        }
    
    }
    
    if (i != PAGE_DIRECTORY_ENTRIES) {
    
        mmu_destroy_address_space ((uintptr_t) child);
        mmu_restore_interrupts (saved_flags);
        
        return 0;
    
    }
    
    mmu_restore_interrupts (saved_flags);
    return (uintptr_t) child;

}

/******************************************************************************
 * @function        mmu_allocate_dma_buffer
 *****************************************************************************/
void *mmu_allocate_dma_buffer (size_t size) {

    uintptr_t saved_flags;
    
    uint64_t page, i, needed, start, end, limit;
    uint32_t pass;
    
    if (!size || size > 0x10000) {
        return 0;
    }
    
    needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    limit = total_page_count;
    
    if (limit > 0x1000000 / PAGE_SIZE) {
        limit = 0x1000000 / PAGE_SIZE;
    }
    
    saved_flags = mmu_save_interrupts ();
    
    for (pass = 0; pass < 2; pass++) {
    
        start = pass ? 0x10000 / PAGE_SIZE : 0x100000 / PAGE_SIZE;
        end = pass ? 0x100000 / PAGE_SIZE : limit;
        
        if (end > limit) {
            end = limit;
        }
        
        for (page = start; page + needed <= end; page++) {
        
            /* An 8237 transfer cannot cross a 64 KiB boundary. */
            if ((page & 15) + needed > 16) {
                continue;
            }
            
            for (i = page; i < page + needed; i++) {
            
                if (mmu_page_used (i)) {
                    break;
                }
            
            }
            
            if (i != page + needed) {
                continue;
            }
            
            for (i = page; i < page + needed; i++) {
                mmu_mark_page_used (i);
            }
            
            i = page + needed - 1;
            allocation_end_bitmap[i >> 5] |= (1UL << (i & 31));
            
            mmu_restore_interrupts (saved_flags);
            return (void *) (uintptr_t) (page * PAGE_SIZE);
        
        }
    
    }
    
    mmu_restore_interrupts (saved_flags);
    return 0;

}

/******************************************************************************
 * @function        kmalloc
 *****************************************************************************/
void *kmalloc (size_t size) {

    uintptr_t saved_flags;
    
    uint32_t page, start, count, needed;
    uint32_t limit;
    
    if (size == 0) {
        return 0;
    }
    
    /* Round the allocation up to complete 4 KiB pages. */
    needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    if (needed == 0 || needed > total_page_count) {
        return 0;
    }
    
    saved_flags = mmu_save_interrupts ();
    
    start = 0;
    count = 0;
    
    limit = total_page_count;
    
    for (page = 1; page < limit; page++) {
    
        if (!mmu_page_used (page)) {
        
            if (count == 0) {
                start = page;
            }
            
            count++;
            
            if (count == needed) {
            
                for (page = start; page < start + needed; page++) {
                    mmu_mark_page_used (page);
                }
                
                page = start + needed - 1;
                
                allocation_end_bitmap[page >> 5] |= (1UL << (page & 31));
                mmu_restore_interrupts (saved_flags);
                
                return (void *) (uintptr_t) (start * PAGE_SIZE);
            
            }
        
        } else {
            count = 0;
        }
    
    }
    
    mmu_restore_interrupts (saved_flags);
    return 0;

}

/******************************************************************************
 * @function        kfree
 *****************************************************************************/
void kfree (void *ptr) {

    uintptr_t saved_flags;
    uint32_t page;
    
    int end;
    
    if (!ptr) {
        return;
    }
    
    if (((uintptr_t) ptr & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    
    page = (uint32_t) ((uintptr_t) ptr / PAGE_SIZE);
    saved_flags = mmu_save_interrupts ();
    
    if (page == 0 || page >= total_page_count || !mmu_page_used (page)) {
    
        mmu_restore_interrupts (saved_flags);
        return;
    
    }
    
    do {
    
        end = ((allocation_end_bitmap[page >> 5] & (1UL << (page & 31))) != 0);
        allocation_end_bitmap[page >> 5] &= ~(1UL << (page & 31));
        
        mmu_mark_page_unused (page);
        page++;
    
    } while (!end && page < total_page_count);
    
    mmu_restore_interrupts (saved_flags);

}

#define     PROT_READ                   0x01
#define     PROT_WRITE                  0x02
#define     PROT_EXEC                   0x04

/******************************************************************************
 * @function        mmu_get_page
 *****************************************************************************/
static uint32_t *mmu_get_page (uintptr_t virtual) {

    uint32_t *directory = mmu_current_page_directory ();
    uint32_t pd_index, pt_index;
    
    uint32_t *pt;
    
    pd_index = (uint32_t) (virtual >> 22);
    pt_index = (uint32_t) ((virtual >> 12) & 0x3FF);
    
    if (!(directory[pd_index] & PG_PRESENT)) {
        return 0;
    }
    
    if (directory[pd_index] & PG_4MB) {
        return 0;
    }
    
    pt = (uint32_t *) (uintptr_t) (directory[pd_index] & PG_ADDRESS);
    return &pt[pt_index];

}

static uintptr_t mmu_user_page_physical (uintptr_t directory, uintptr_t virtual, int writable) {

    uint32_t entry, required = PG_PRESENT | PG_USER;
    uint32_t *pd, *pt;
    
    if (writable) {
        required |= PG_WRITE;
    }
    
    pd = (uint32_t *) directory;
    entry = pd[virtual >> 22];
    
    if ((entry & required) != required || (entry & PG_4MB)) {
        return 0;
    }
    
    pt = (uint32_t *) (uintptr_t) (entry & PG_ADDRESS);
    entry = pt[(virtual >> 12) & 0x3FF];
    
    if ((entry & required) != required) {
        return 0;
    }
    
    return (uintptr_t) (entry & PG_ADDRESS) | (virtual & (PAGE_SIZE - 1));

}

int mmu_user_buffer_valid (uintptr_t pd, uintptr_t address, size_t size, int writable) {

    uintptr_t offset;
    
    if (!pd || !size || address < PAGE_SIZE || size - 1 > (uintptr_t) -1 - address) {
        return 0;
    }
    
    offset = 0;
    
    while (offset < size) {
    
        if (!mmu_user_page_physical (pd, address + offset, writable)) {
            return 0;
        }
        
        offset += PAGE_SIZE - ((address + offset) & (PAGE_SIZE - 1));
    
    }
    
    return 1;

}

/* Copy through the physical identity map, without changing CR3. */
int mmu_copy_user (uintptr_t pd, uintptr_t address, void *buffer, size_t size, int to_user) {

    uintptr_t physical;
    size_t offset, chunk;
    
    if (!buffer || !mmu_user_buffer_valid (pd, address, size, to_user)) {
        return 0;
    }
    
    offset = 0;
    
    while (offset < size) {
    
        physical = mmu_user_page_physical (pd, address + offset, to_user);
        chunk = PAGE_SIZE - ((address + offset) & (PAGE_SIZE - 1));
        
        if (chunk > size - offset) {
            chunk = size - offset;
        }
        
        if (to_user) {
            memcpy ((void *) physical, (const uint8_t *) buffer + offset, chunk);
        } else {
            memcpy ((uint8_t *) buffer + offset, (const void *) physical, chunk);
        }
        
        offset += chunk;
    
    }
    
    return 1;

}

/******************************************************************************
 * @function        mmu_unmap_page
 *****************************************************************************/
static int mmu_unmap_page (uintptr_t virtual) {

    uintptr_t physical;
    uint32_t *pte;
    
    pte = mmu_get_page (virtual);
    
    if (!pte || !(*pte & PG_PRESENT) || !(*pte & PG_USER)) {
        return 0;
    }
    
    physical = (uintptr_t) (*pte & PG_ADDRESS);
    *pte = 0;
    
    __asm__ __volatile__ (
        "mov cr3, %0"
        :
        : "r" ((uintptr_t) mmu_current_page_directory ())
        : "memory"
    );
    
    if (physical) {
        mmu_mark_page_unused ((uint32_t) (physical / PAGE_SIZE));
    }
    
    return 1;

}

/******************************************************************************
 * @function        mmu_mmap_range_free
 *****************************************************************************/
static int mmu_mmap_range_free (uintptr_t virtual, size_t size) {

    uint32_t *directory = mmu_current_page_directory ();
    size_t offset;
    
    uint32_t *pte;
    uint32_t pd_index;
    
    for (offset = 0; offset < size; offset += PAGE_SIZE) {
    
        pd_index = (uint32_t) ((virtual + offset) >> 22);
        
        /**
         * The mmap area lies inside the initial 4 GiB identity mapping.
         * An unsplit 4 MiB PDE is available to mmap; mmu_map_page() will
         * split it when the first 4 KiB page in that region is allocated.
         */
        if (directory[pd_index] & PG_4MB) {
            continue;
        }
        
        pte = mmu_get_page (virtual + offset);
        
        if (pte && (*pte & PG_PRESENT)) {
            return 0;
        }
    
    }
    
    return 1;

}

#define     MMAP_BASE                   0xC0000000UL

/******************************************************************************
 * @function        mmap
 *****************************************************************************/
void *mmap (void *addr, size_t length, int prot, int flags, int fd, uintptr_t offset) {

    uintptr_t physical, virtual, saved_flags;
    uint32_t page_flags;
    
    size_t mapped, size;
    
    (void) fd;
    (void) offset;
    
    if (!length) {
        return (void *) (intptr_t) -EINVAL;
    }
    
    /**
     * File-backed mappings are not implemented yet.  Anonymous mappings
     * are backed by newly allocated physical pages below.
     */
    if (!(flags & MAP_ANONYMOUS)) {
        return (void *) (intptr_t) -ENODEV;
    }
    
    if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) {
        return (void *) (intptr_t) -EINVAL;
    }
    
    if (length > (size_t) -1 - (PAGE_SIZE - 1)) {
        return (void *) (intptr_t) -ENOMEM;
    }
    
    size = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    saved_flags = mmu_save_interrupts ();
    
    if (addr) {
    
        virtual = (uintptr_t) addr;
        virtual &= ~((uintptr_t) PAGE_SIZE - 1);
        
        if (virtual < MMAP_BASE || size - 1 > (uintptr_t) -1 - virtual) {
        
            mmu_restore_interrupts (saved_flags);
            return (void *) (intptr_t) -EINVAL;
        
        }
        
        if (!mmu_mmap_range_free (virtual, size)) {
        
            mmu_restore_interrupts (saved_flags);
            return (void *) (intptr_t) -ENOMEM;
        
        }
    
    } else {
    
        virtual = MMAP_BASE;
        
        while (size - 1 <= (uintptr_t) -1 - virtual && !mmu_mmap_range_free (virtual, size)) {
        
            if (virtual > 0xFFFFFFFFUL - PAGE_SIZE) {
            
                mmu_restore_interrupts (saved_flags);
                return (void *) (intptr_t) -ENOMEM;
            
            }
            
            virtual += PAGE_SIZE;
        
        }
        
        if (size - 1 > (uintptr_t) -1 - virtual) {
        
            mmu_restore_interrupts (saved_flags);
            return (void *) (intptr_t) -ENOMEM;
        
        }
    
    }
    
    page_flags = PG_USER;
    
    if (prot & PROT_WRITE) {
        page_flags |= PG_WRITE;
    }
    
    mapped = 0;
    
    while (mapped < size) {
    
        physical = mmu_allocate_page ();
        
        if (!physical) {
            break;
        }
        
        if (!mmu_map_page (virtual + mapped, physical, page_flags, flags)) {
        
            mmu_mark_page_unused ((uint32_t) (physical / PAGE_SIZE));
            break;
        
        }
        
        mapped += PAGE_SIZE;
    
    }
    
    if (mapped != size) {
    
        while (mapped) {
        
            mapped -= PAGE_SIZE;
            mmu_unmap_page (virtual + mapped);
        
        }
        
        mmu_restore_interrupts (saved_flags);
        return (void *) (intptr_t) -ENOMEM;
    
    }
    
    //dprintf ("[mmap] pd=%p addr=%p size=%u kB\n", (void *) mmu_current_page_directory (), (void *) virtual, (unsigned) (size / 1024));
    
    mmu_restore_interrupts (saved_flags);
    return (void *) virtual;

}

/******************************************************************************
 * @function        munmap
 *****************************************************************************/
int munmap (void *addr, size_t length) {

    uintptr_t virtual, saved_flags;
    size_t unmapped, size, released = 0;
    
    //dprintf ("[munmap] pd=%p addr=%p length=%u\n", (void *) mmu_current_page_directory (), addr, (unsigned) length);
    
    if (!addr || !length) {
        return (void *) (intptr_t) -EINVAL;
    }
    
    virtual = (uintptr_t) addr;
    
    if (virtual & (PAGE_SIZE - 1)) {
        return (void *) (intptr_t) -EINVAL;
    }
    
    if (length > (size_t) -1 - (PAGE_SIZE - 1)) {
        return (void *) (intptr_t) -EINVAL;
    }
    
    size = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    saved_flags = mmu_save_interrupts ();
    
    for (unmapped = 0; unmapped < size; unmapped += PAGE_SIZE) {
    
        if (!mmu_get_page (virtual + unmapped)) {
        
            mmu_restore_interrupts (saved_flags);
            return (void *) (intptr_t) -EINVAL;
        
        }
    
    }
    
    for (unmapped = 0; unmapped < size; unmapped += PAGE_SIZE) {
    
        if (mmu_unmap_page (virtual + unmapped)) {
            released += PAGE_SIZE;
        }
    
    }
    
    //dprintf ("[munmap] pd=%p addr=%p released=%u kB\n", (void *) mmu_current_page_directory (), addr, (unsigned) (released / 1024));
    
    mmu_restore_interrupts (saved_flags);
    return 0;

}

/******************************************************************************
 * @file            mmu.c
 *****************************************************************************/
#ifndef     __AMD64__
# define        __AMD64__               /* Stop Visual Studio Code complaining. */
#endif

#include    <corazon/mmu.h>

#include    <errno.h>
#include    <stddef.h>
#include    <stdint.h>
#include    <string.h>

/******************************************************************************
 * Paging constants.
 *****************************************************************************/
#define     PAGESIZE_2M                 0x0000000000200000ULL

#define     PG_PRESENT                  0x0000000000000001ULL
#define     PG_WRITE                    0x0000000000000002ULL
#define     PG_2MB                      0x0000000000000080ULL

#define     PAGE_DIRECTORY_COUNT        257
#define     PAGE_TABLE_ENTRIES          512

#define     PAGE_TABLE_COUNT            (2 + PAGE_DIRECTORY_COUNT)

/******************************************************************************
 * Physical memory information.
 *****************************************************************************/
static uint64_t total_page_count = 0;

/**
 * The paging structures used for the identity map are:
 *
 *     1 x PML4
 *     1 x PDPT
 * 
 *     257 x page directory
 *
 * The final page directory maps the 1 GiB region beginning at
 * 0x4000000000.  Allocate one additional page so a
 * 4 KiB-aligned PAGE_TABLE_COUNT-page region always exists inside this array
 * without relying on compiler-specific alignment support.
 */
static uint64_t page_table_storage[(PAGE_TABLE_COUNT + 1) * (PAGE_SIZE / sizeof (uint64_t))];

static uint64_t *kernel_pml4 = 0;
uintptr_t kernel_page_directory_physical = 0;

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
    
    __asm__ __volatile__ ("pushfq");
    __asm__ __volatile__ ("pop %0" : "=r" (flags) : : "memory");
    
    __asm__ __volatile__ ("cli");
    return flags;

}

/******************************************************************************
 * @function        mmu_restore_interrupts
 *****************************************************************************/
static void mmu_restore_interrupts (uintptr_t flags) {

    __asm__ __volatile__ ("push %0" : : "r" (flags) : "memory");
    __asm__ __volatile__ ("popfq");

}

/******************************************************************************
 * @function        mmu_initialize
 *****************************************************************************/
void mmu_initialize (uint32_t high_mem) {

    uintptr_t tables;
    
    uint64_t *pml4, *pdpt, *pd;
    uint64_t physical;
    
    uint32_t i, j;
    
    /* Mark all memory as unavailable. */
    for (i = 0; i < PAGE_BITMAP_SIZE; i++) {
    
        page_bitmap[i] = 0xFFFFFFFFUL;
        allocation_end_bitmap[i] = 0;
    
    }
    
    /**
     * high_mem is expressed in KiB.
     *
     * This describes installed RAM only.  MMIO such as the GOP framebuffer
     * may exist outside that RAM and is mapped separately by the identity
     * mappings below.
     */
    total_page_count = ((uint64_t) high_mem * 1024ULL) / PAGE_SIZE;
    
    if (total_page_count > MAX_PAGE_COUNT) {
        total_page_count = MAX_PAGE_COUNT;
    }
    
    /* Find a 4 KiB-aligned paging-structure region inside page_table_storage. */
    tables = (uintptr_t) page_table_storage;
    tables += PAGE_SIZE - 1;
    tables &= ~((uintptr_t) PAGE_SIZE - 1);
    
    pml4 = (uint64_t *) tables;
    pdpt = (uint64_t *) (tables + PAGE_SIZE);
    
    /**
     * corazon is currently identity mapped, so these virtual addresses are also
     * the physical addresses used in the paging structures.
     */
    kernel_pml4 = pml4;
    kernel_page_directory_physical = (uint64_t) (uintptr_t) pml4;
    
    /* Clear all paging-structure pages. */
    for (i = 0; i < PAGE_TABLE_COUNT * PAGE_TABLE_ENTRIES; i++) {
        pml4[i] = 0;
    }
    
    /* PML4[0] covers the low 512 GiB of virtual address space. */
    pml4[0] = ((uint64_t) (uintptr_t) pdpt) | PG_PRESENT | PG_WRITE;
    
    /**
     * Identity-map the first 257 GiB using 2 MiB pages.
     *
     * PDPT[0] through PDPT[255] map the first 256 GiB.  PDPT[256] maps
     * the complete 1 GiB region beginning at physical address 0x4000000000.
     */
    physical = 0;
    
    for (i = 0; i < PAGE_DIRECTORY_COUNT; i++) {
    
        pd = (uint64_t *) (tables + ((uintptr_t) (i + 2) * PAGE_SIZE));
        pdpt[i] = ((uint64_t) (uintptr_t) pd) | PG_PRESENT | PG_WRITE;
        
        for (j = 0; j < PAGE_TABLE_ENTRIES; j++) {
        
            pd[j] = physical | PG_PRESENT | PG_WRITE | PG_2MB;
            physical += PAGESIZE_2M;
        
        }
    
    }
    
    /*
     * Long mode is already active when the AMD64 kernel reaches this
     * function, so paging and CR4.PAE are already enabled.  Install the new
     * PML4 by reloading CR3.
     */
    __asm__ __volatile__ ("mov cr3, %0" : : "r" (kernel_page_directory_physical));

}

/******************************************************************************
 * @function        mmu_mark_page_used
 *****************************************************************************/
static void mmu_mark_page_used (uint64_t page) {
    page_bitmap[page >> 5] |= (1UL << (page & 31));
}

/******************************************************************************
 * @function        mmu_page_used
 *****************************************************************************/
static int mmu_page_used (uint64_t page) {
    return ((page_bitmap[page >> 5] & (1UL << (page & 31))) != 0);
}

/******************************************************************************
 * @function        mmu_mark_page_unused
 *****************************************************************************/
static void mmu_mark_page_unused (uint64_t page) {
    page_bitmap[page >> 5] &= ~(1UL << (page & 31));
}

/******************************************************************************
 * @function        mmu_mark_region_unused
 *****************************************************************************/
void mmu_mark_region_unused (uint64_t base, uint64_t length) {

    uintptr_t saved_flags;
    
    uint64_t start, end;
    uint64_t page;
    
    uint64_t tables_start = kernel_page_directory_physical;
    uint64_t tables_end   = tables_start + (PAGE_TABLE_COUNT * PAGE_SIZE);
    
    start = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    end = (base + length) & ~(PAGE_SIZE - 1);
    
    if (start >= PHYSICAL_LIMIT) {
        return;
    }
    
    if (end > PHYSICAL_LIMIT) {
        end = PHYSICAL_LIMIT;
    }
    
    saved_flags = mmu_save_interrupts ();
    
    for (page = start / PAGE_SIZE; page < end / PAGE_SIZE && page < total_page_count; page++) {
    
        uint64_t address = page * PAGE_SIZE;
        
        /* Never release memory up to 0x500. */
        if (address < 0x500) {
            continue;
        }
        
        /* Keep 0x10000 – 0x1FFFF for the floppy dma buffer. */
        if (address >= 0x10000 && address < 0x20000) {
            continue;
        }
        
        /* Never release the kernel image. */
        if (address >= kernel_start && address < kernel_start + kernel_end) {
            continue;
        }
        
        /* Never release the active paging structures. */
        if (address >= tables_start && address < tables_end) {
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
    
    if (start >= PHYSICAL_LIMIT) {
        return;
    }
    
    if (end > PHYSICAL_LIMIT) {
        end = PHYSICAL_LIMIT;
    }
    
    saved_flags = mmu_save_interrupts ();
    
    for (page = start / PAGE_SIZE; page < end / PAGE_SIZE && page < total_page_count; page++) {
        mmu_mark_page_used (page);
    }
    
    mmu_restore_interrupts (saved_flags);

}

/******************************************************************************
 * @function        mmu_get_memory_info
 *****************************************************************************/
void mmu_get_memory_info (uint64_t *total, uint64_t *free) {

    uintptr_t saved_flags;
    
    uint64_t free_pages = 0;
    uint64_t page;
    
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

#define     PG_PRESENT                  0x0000000000000001ULL
#define     PG_WRITE                    0x0000000000000002ULL
#define     PG_USER                     0x0000000000000004ULL
#define     PG_2MB                      0x0000000000000080ULL

#define     PG_ADDRESS                  0x000FFFFFFFFFF000ULL

/******************************************************************************
 * @function        mmu_current_pml4
 *****************************************************************************/
static uint64_t *mmu_current_pml4 (void) {

    uintptr_t cr3;
    
    __asm__ __volatile__ ("mov %0, cr3" : "=r" (cr3));
    return (uint64_t *) (cr3 & PG_ADDRESS);

}

/******************************************************************************
 * @function        mmu_allocate_page
 *****************************************************************************/
static uintptr_t mmu_allocate_page (void) {

    uint64_t page;
    
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
#define     MAP_32BIT                   0x40

/******************************************************************************
 * @function        mmu_split_page
 *****************************************************************************/
static uint64_t *mmu_split_page (uint64_t *pd, uint64_t index, int flags) {

    uint64_t *pt;
    
    uint64_t physical;
    uint64_t page_flags;
    
    uintptr_t page;
    uint32_t i;
    
    if (!(pd[index] & PG_2MB)) {
        return (uint64_t *) (uintptr_t) (pd[index] & PG_ADDRESS);
    }
    
    page = mmu_allocate_page ();
    
    if (!page) {
        return 0;
    }
    
    pt = (uint64_t *) page;
    
    physical = pd[index] & PG_ADDRESS;
    page_flags = pd[index] & (PG_PRESENT | PG_WRITE | PG_USER);
    
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
    
        if (flags & MAP_ANONYMOUS) {
            pt[i] = 0;
        } else {
            pt[i] = physical | page_flags;
        }
        
        physical += PAGE_SIZE;
    
    }
    
    pd[index] = ((uint64_t) page) | page_flags;
    
    __asm__ __volatile__ ("mov cr3, %0" : : "r" ((uintptr_t) mmu_current_pml4 ()));
    return pt;

}

/******************************************************************************
 * @function        mmu_map_page
 *****************************************************************************/
static int mmu_map_page (uintptr_t virtual, uintptr_t physical, uint64_t flags, int mmap_flags) {

    uint64_t *pml4, *pdpt, *pd, *pt;
    
    uint64_t pml4_index, pdpt_index, pd_index, pt_index, i;
    uintptr_t page;
    
    pml4_index = (virtual >> 39) & 0x1FF;
    pdpt_index = (virtual >> 30) & 0x1FF;
    
    pd_index = (virtual >> 21) & 0x1FF;
    pt_index = (virtual >> 12) & 0x1FF;
    
    pml4 = mmu_current_pml4 ();
    
    if (!(pml4[pml4_index] & PG_PRESENT)) {
    
        page = mmu_allocate_page ();
        
        if (!page) {
            return 0;
        }
        
        pt = (uint64_t *) page;
        
        for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            pt[i] = 0;
        }
        
        pml4[pml4_index] = ((uint64_t) page) | PG_PRESENT | PG_WRITE | (flags & PG_USER);
    
    }
    
    if (flags & PG_USER) {
        pml4[pml4_index] |= PG_USER;
    }
    
    pdpt = (uint64_t *) (uintptr_t) (pml4[pml4_index] & PG_ADDRESS);
    
    if (!(pdpt[pdpt_index] & PG_PRESENT)) {
    
        page = mmu_allocate_page ();
        
        if (!page) {
            return 0;
        }
        
        pd = (uint64_t *) page;
        
        for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            pd[i] = 0;
        }
        
        pdpt[pdpt_index] = ((uint64_t) page) | PG_PRESENT | PG_WRITE | (flags & PG_USER);
    
    }
    
    if (flags & PG_USER) {
        pdpt[pdpt_index] |= PG_USER;
    }
    
    pd = (uint64_t *) (uintptr_t) (pdpt[pdpt_index] & PG_ADDRESS);
    
    if (pd[pd_index] & PG_2MB) {
    
        pt = mmu_split_page (pd, pd_index, mmap_flags);
        
        if (!pt) {
            return 0;
        }
    
    } else if (!(pd[pd_index] & PG_PRESENT)) {
    
        page = mmu_allocate_page ();
        
        if (!page) {
            return 0;
        }
        
        pt = (uint64_t *) page;
        
        for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            pt[i] = 0;
        }
        
        pd[pd_index] = ((uint64_t) page) | PG_PRESENT | PG_WRITE | (flags & PG_USER);
    
    } else {
        pt = (uint64_t *) (uintptr_t) (pd[pd_index] & PG_ADDRESS);
    }
    
    if (flags & PG_USER) {
        pd[pd_index] |= PG_USER;
    }
    
    pt[pt_index] = ((uint64_t) physical & PG_ADDRESS) |PG_PRESENT | flags;
    
    __asm__ __volatile__ (
        "invlpg [%0]"
        :
        : "r" (virtual)
    );
    
    return 1;

}

/******************************************************************************
 * @function        mmu_free_cloned_table
 *****************************************************************************/
static void mmu_free_cloned_table (uint64_t *table, int level) {

    uint64_t entry;
    uint32_t i;
    
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
    
        entry = table[i];
        
        if (!(entry & PG_PRESENT)) {
            continue;
        }
        
        if (level == 1) {
        
            if (entry & PG_USER) {
                mmu_mark_page_unused ((entry & PG_ADDRESS) / PAGE_SIZE);
            }
        
        } else if (!(entry & PG_2MB)) {
            mmu_free_cloned_table ((uint64_t *) (uintptr_t) (entry & PG_ADDRESS), level - 1);
        }
    
    }
    
    mmu_mark_page_unused ((uintptr_t) table / PAGE_SIZE);

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

    uintptr_t saved_flags;
    
    if (!pd || pd == kernel_page_directory_physical) {
        return;
    }
    
    saved_flags = mmu_save_interrupts ();
    
    mmu_free_cloned_table ((uint64_t *) pd, 4);
    mmu_restore_interrupts (saved_flags);

}

/******************************************************************************
 * @function        mmu_clone_table
 *****************************************************************************/
static uintptr_t mmu_clone_table (uint64_t *parent, int level) {

    uintptr_t page, copied;
    
    uint64_t *child;
    uint64_t entry;
    
    uint32_t i;
    
    if (!(page = mmu_allocate_page ())) {
        return 0;
    }
    
    child = (uint64_t *) page;
    
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        child[i] = 0;
    }
    
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++) {
    
        entry = parent[i];
        
        if (!(entry & PG_PRESENT) || (level > 1 && (entry & PG_2MB) && !(entry & PG_USER))) {
            child[i] = entry;
            continue;
        }
        
        if (level > 1 && (entry & PG_2MB)) {
            break;
        }
        
        if (level == 1 && !(entry & PG_USER)) {
            child[i] = entry;
            continue;
        }
        
        if (level == 1) {
        
            copied = mmu_allocate_page ();
            
            if (!copied) {
                break;
            }
            
            memcpy ((void *) copied, (void *) (uintptr_t) (entry & PG_ADDRESS), PAGE_SIZE);
        
        } else {
        
            copied = mmu_clone_table ((uint64_t *) (uintptr_t) (entry & PG_ADDRESS), level - 1);
            
            if (!copied) {
                break;
            }
        
        }
        
        child[i] = ((uint64_t) copied & PG_ADDRESS) | (entry & ~PG_ADDRESS);
    
    }
    
    if (i != PAGE_TABLE_ENTRIES) {
    
        mmu_free_cloned_table (child, level);
        return 0;
    
    }
    
    return (uintptr_t) child;

}

/******************************************************************************
 * @function        mmu_clone_address_space
 *****************************************************************************/
uintptr_t mmu_clone_address_space (uintptr_t parent_pd) {

    uintptr_t saved_flags, child;
    
    if (!parent_pd) {
        return 0;
    }
    
    saved_flags = mmu_save_interrupts ();
    
    child = mmu_clone_table ((uint64_t *) parent_pd, 4);
    mmu_restore_interrupts (saved_flags);
    
    return child;

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
    
    if (limit > 4096) {
        limit = 4096;
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
    uint64_t page, start, count, needed;
    
    if (size == 0) {
        return 0;
    }
    
    needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    if (needed == 0 || needed > total_page_count) {
        return 0;
    }
    
    saved_flags = mmu_save_interrupts ();
    
    start = 0;
    count = 0;
    
    for (page = 1; page < total_page_count; page++) {
    
        if (!mmu_page_used (page)) {
        
            if (count == 0) {
                start = page;
            }
            
            count++;
            
            if (count == needed) {
            
                uint64_t i;
                
                for (i = start; i < start + needed; i++) {
                    mmu_mark_page_used (i);
                }
                
                i = start + needed - 1;
                
                allocation_end_bitmap[i >> 5] |= (1UL << (i & 31));
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
    uint64_t page;
    
    int end;
    
    if (!ptr) {
        return;
    }
    
    if (((uintptr_t) ptr & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    
    page = (uint64_t) ((uintptr_t) ptr / PAGE_SIZE);
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
static uint64_t *mmu_get_page (uintptr_t virtual) {

    uint64_t *pml4, *pdpt, *pd, *pt;
    uint64_t pml4_index, pdpt_index, pd_index, pt_index;
    
    pml4_index = (virtual >> 39) & 0x1FF;
    pdpt_index = (virtual >> 30) & 0x1FF;
    
    pd_index = (virtual >> 21) & 0x1FF;
    pt_index = (virtual >> 12) & 0x1FF;
    
    pml4 = mmu_current_pml4 ();
    
    if (!(pml4[pml4_index] & PG_PRESENT)) {
        return 0;
    }
    
    pdpt = (uint64_t *) (uintptr_t) (pml4[pml4_index] & PG_ADDRESS);
    
    if (!(pdpt[pdpt_index] & PG_PRESENT)) {
        return 0;
    }
    
    pd = (uint64_t *) (uintptr_t) (pdpt[pdpt_index] & PG_ADDRESS);
    
    if (!(pd[pd_index] & PG_PRESENT)) {
        return 0;
    }
    
    if (pd[pd_index] & PG_2MB) {
        return 0;
    }
    
    pt = (uint64_t *) (uintptr_t) (pd[pd_index] & PG_ADDRESS);
    return &pt[pt_index];

}

static uintptr_t mmu_user_page_physical (uintptr_t directory, uintptr_t virtual, int writable) {

    uint64_t *pml4, *pdpt, *pd, *pt;
    uint64_t entry, required = PG_PRESENT | PG_USER;
    
    if (writable) {
        required |= PG_WRITE;
    }
    
    pml4 = (uint64_t *) directory;
    entry = pml4[(virtual >> 39) & 0x1FF];
    
    if ((entry & required) != required) {
        return 0;
    }
    
    pdpt = (uint64_t *) (uintptr_t) (entry & PG_ADDRESS);
    entry = pdpt[(virtual >> 30) & 0x1FF];
    
    if ((entry & required) != required) {
        return 0;
    }
    
    pd = (uint64_t *) (uintptr_t) (entry & PG_ADDRESS);
    entry = pd[(virtual >> 21) & 0x1FF];
    
    if ((entry & required) != required || (entry & PG_2MB)) {
        return 0;
    }
    
    pt = (uint64_t *) (uintptr_t) (entry & PG_ADDRESS);
    entry = pt[(virtual >> 12) & 0x1FF];
    
    if ((entry & required) != required) {
        return 0;
    }
    
    return (uintptr_t) (entry & PG_ADDRESS) | (virtual & (PAGE_SIZE - 1));

}

int mmu_user_buffer_valid (uintptr_t pd, uintptr_t address, size_t size, int writable) {

    uintptr_t offset;
    
    if (!pd || !size || address < PAGE_SIZE || address >= 0x0000800000000000ULL || size > 0x0000800000000000ULL - address) {
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

    uint64_t *pte;
    uintptr_t physical;
    
    pte = mmu_get_page (virtual);
    
    if (!pte || !(*pte & PG_PRESENT) || !(*pte & PG_USER)) {
        return 0;
    }
    
    physical = (uintptr_t) (*pte & PG_ADDRESS);
    *pte = 0;
    
    __asm__ __volatile__ ("invlpg [%0]" : : "r" (virtual));
    
    if (physical) {
        mmu_mark_page_unused (physical / PAGE_SIZE);
    }
    
    return 1;

}

/******************************************************************************
 * @function        mmu_mmap_range_free
 *****************************************************************************/
static int mmu_mmap_range_free (uintptr_t virtual, size_t size) {

    size_t offset;
    
    uint64_t *pml4, *pdpt, *pd;
    uint64_t *pte;
    
    uint64_t pml4_index, pdpt_index, pd_index;
    
    for (offset = 0; offset < size; offset += PAGE_SIZE) {
    
        pml4_index = ((virtual + offset) >> 39) & 0x1FF;
        pdpt_index = ((virtual + offset) >> 30) & 0x1FF;
        
        pd_index = ((virtual + offset) >> 21) & 0x1FF;
        pml4 = mmu_current_pml4 ();
        
        if (!(pml4[pml4_index] & PG_PRESENT)) {
            continue;
        }
        
        pdpt = (uint64_t *) (uintptr_t) (pml4[pml4_index] & PG_ADDRESS);
        
        if (!(pdpt[pdpt_index] & PG_PRESENT)) {
            continue;
        }
        
        pd = (uint64_t *) (uintptr_t) (pdpt[pdpt_index] & PG_ADDRESS);
        
        if (!(pd[pd_index] & PG_PRESENT) || (pd[pd_index] & PG_2MB)) {
            continue;
        }
        
        pte = mmu_get_page (virtual + offset);
        
        if (pte && (*pte & PG_PRESENT)) {
            return 0;
        }
    
    }
    
    return 1;

}

#define     MMAP_BASE                   0x0000004040000000ULL
#define     MMAP_32BIT_BASE             0x0000000040000000ULL
#define     MMAP_32BIT_LIMIT            0x0000000080000000ULL

/******************************************************************************
 * @function        mmap
 *****************************************************************************/
void *mmap (void *addr, size_t length, int prot, int flags, int fd, uintptr_t offset) {

    uintptr_t physical, virtual, saved_flags;
    uint64_t page_flags;
    
    size_t mapped, size;
    
    (void) offset;
    (void) fd;
    
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
        
        if ((virtual < MMAP_32BIT_BASE || (virtual >= MMAP_32BIT_LIMIT && virtual < MMAP_BASE)) || (!(flags & MAP_32BIT) && virtual < MMAP_BASE)) {
        
            mmu_restore_interrupts (saved_flags);
            return (void *) (intptr_t) -EINVAL;
        
        }
        
        if ((flags & MAP_32BIT) && virtual >= MMAP_32BIT_LIMIT) {
        
            mmu_restore_interrupts (saved_flags);
            return (void *) (intptr_t) -EINVAL;
        
        }
        
        if (!mmu_mmap_range_free (virtual, size)) {
        
            mmu_restore_interrupts (saved_flags);
            return (void *) (intptr_t) -ENOMEM;
        
        }
    
    } else if (flags & MAP_32BIT) {
    
        virtual = MMAP_32BIT_BASE;
        
        while (virtual < MMAP_32BIT_LIMIT) {
        
            if (size <= MMAP_32BIT_LIMIT - virtual && mmu_mmap_range_free (virtual, size)) {
                break;
            }
            
            virtual += PAGE_SIZE;
        
        }
        
        if (virtual >= MMAP_32BIT_LIMIT || size > MMAP_32BIT_LIMIT - virtual) {
        
            mmu_restore_interrupts (saved_flags);
            return (void *) (intptr_t) -ENOMEM;
        
        }
    
    } else {
    
        virtual = MMAP_BASE;
        
        while (!mmu_mmap_range_free (virtual, size)) {
        
            if (virtual > (uintptr_t) -1 - PAGE_SIZE) {
            
                mmu_restore_interrupts (saved_flags);
                return (void *) (intptr_t) -ENOMEM;
            
            }
            
            virtual += PAGE_SIZE;
        
        }
        
        if (virtual > (uintptr_t) -1 - size) {
        
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
        
            mmu_mark_page_unused (physical / PAGE_SIZE);
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
        return 0;
    
    }
    
    mmu_restore_interrupts (saved_flags);
    return (void *) virtual;

}

/******************************************************************************
 * @function        munmap
 *****************************************************************************/
int munmap (void *addr, size_t length) {

    uintptr_t virtual, saved_flags;
    size_t unmapped, size;
    
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
        mmu_unmap_page (virtual + unmapped);
    }
    
    mmu_restore_interrupts (saved_flags);
    return 0;

}

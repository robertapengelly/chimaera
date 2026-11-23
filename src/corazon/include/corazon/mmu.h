/******************************************************************************
 * @file            mmu.h
 *****************************************************************************/
#ifndef     _MMU_H
#define     _MMU_H

#include    <stddef.h>
#include    <stdint.h>

void mmu_mark_region_unused (uint64_t base, uint64_t length);
void mmu_mark_region_used (uint64_t base, uint64_t length);

void mmu_get_memory_info (uint64_t *total, uint64_t *free);
void *mmu_allocate_dma_buffer (size_t size);

#if     defined (__AMD64__)
void mmu_initialize (uint32_t high_mem);
#else
void mmu_initialize (uint32_t high_mem, uintptr_t reserved_end);
#endif

uintptr_t mmu_clone_address_space (uintptr_t parent_pd);
void mmu_destroy_address_space (uintptr_t pd);

int mmu_install_user_page (uintptr_t pd, uintptr_t virtual, const void *contents, size_t size);

int mmu_user_buffer_valid (uintptr_t pd, uintptr_t address, size_t size, int writable);
int mmu_copy_user (uintptr_t pd, uintptr_t address, void *buffer, size_t size, int to_user);

#if     defined (__AMD64__)
# define    PAGE_SIZE                   0x1000ULL

# define    PHYSICAL_LIMIT              0x4000000000ULL
# define    MAX_PAGE_COUNT              (PHYSICAL_LIMIT / PAGE_SIZE)
#else
# define    PAGE_SIZE                   0x1000UL

# define    PHYSICAL_LIMIT              0x100000000ULL
# define    MAX_PAGE_COUNT              (PHYSICAL_LIMIT / PAGE_SIZE)
#endif

#define     PAGE_INDEX_4M(addr)         ((addr) >> 22)
extern uintptr_t kernel_page_directory_physical;

#endif      /* _MMU_H */

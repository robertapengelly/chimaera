/******************************************************************************
 * @file            gdt.c
 *****************************************************************************/
#ifndef     __AMD64__
# define        __AMD64__               /* Stop Visual Studio Code complaining. */
#endif

#include    <corazon/tss.h>

#include    <stdint.h>
#include    <string.h>

__attribute__ ((packed)) typedef struct {

    /* Limits */
    uint16_t limit_low;
    
    /* Segment address */
    uint16_t base_low;
    uint8_t base_middle;
    
    /* Access modes */
    uint8_t access;
    uint8_t granularity;
    
    uint8_t base_high;

} gdt_entry_t;

__attribute__((packed)) typedef struct {

    uint32_t base_highest;
    uint32_t reserved0;

} gdt_entry_high_t;

__attribute__ ((packed)) typedef struct {

    uint16_t limit;
    uintptr_t base;

} gdt_pointer_t;

__attribute__((packed, aligned (0x10))) typedef struct  {

    gdt_entry_t entries[7];
    gdt_entry_high_t tss_extra;
    gdt_pointer_t pointer;
    tss_entry_t tss;

} full_gdt_t;

extern void *stack_top;

full_gdt_t gdt[32] = { {

    {
    
        { 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00 },
        
        { 0xFFFF, 0x0000, 0x00, 0x9A, (1 << 5) | (1 << 7) | 0x0F, 0x00 },
        { 0xFFFF, 0x0000, 0x00, 0x92, (1 << 6) | (1 << 7) | 0x0F, 0x00 },
        { 0xFFFF, 0x0000, 0x00, 0xFA, (1 << 6) | (1 << 7) | 0x0F, 0x00 },
        { 0xFFFF, 0x0000, 0x00, 0xF2, (1 << 6) | (1 << 7) | 0x0F, 0x00 },
        { 0xFFFF, 0x0000, 0x00, 0xFA, (1 << 5) | (1 << 7) | 0x0F, 0x00 },
        
        { 0x0067, 0x0000, 0x00, 0x89, 0x00, 0x00 },
    
    },
    
    { 0x00000000, 0x00000000 },
    { 0x0000, 0x0000000000000000 },
    
    { 0, { 0, 0, 0 }, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0, 0, 0 },

} };

extern void gdt_flush (uintptr_t);

tss_entry_t *g_tss;
uintptr_t syscall_stack;

uintptr_t syscall_user_rcx;
uintptr_t syscall_user_r11;
uintptr_t syscall_user_rsp;
uintptr_t syscall_user_rax;

void gdt_install (void) {

    gdt_pointer_t *gdtp = &gdt->pointer;
    int i;
    
    for (i = 1; i < 32; i++) {
        memcpy (&gdt[i], &gdt[0], sizeof (*gdt));
    }
    
    g_tss = &gdt[0].tss;
    
    for (i = 0; i < 32; i++) {
    
        uintptr_t addr = (uintptr_t) &gdt[i].tss;
        
        gdt[i].pointer.limit = sizeof (gdt[i].entries) + sizeof (gdt[i].tss_extra) - 1;
        gdt[i].pointer.base  = (uintptr_t) &gdt[i].entries;
        
        gdt[i].entries[6].limit_low = sizeof (gdt[i].tss);
        gdt[i].entries[6].base_low = (addr & 0xFFFF);
        gdt[i].entries[6].base_middle = (addr >> 16) & 0xFF;
        gdt[i].entries[6].base_high = (addr >> 24) & 0xFF;
        
        gdt[i].tss_extra.base_highest = (addr >> 32) & 0xFFFFFFFF;
    
    }
    
    g_tss->rsp[0] = (uintptr_t) &stack_top;
    syscall_stack = g_tss->rsp[0];
    
    /* Flush the GDT. */
    gdt_flush ((uintptr_t) gdtp);
    
    /* Flush the TSS. */
    tss_flush ();

}

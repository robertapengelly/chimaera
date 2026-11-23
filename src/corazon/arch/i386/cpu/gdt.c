/******************************************************************************
 * @file            gdt.c
 *****************************************************************************/
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

__attribute__ ((packed)) typedef struct {

    uint16_t limit;
    uintptr_t base;

} gdt_pointer_t;

__attribute__ ((packed)) static struct {

    gdt_entry_t entries[7];
    gdt_pointer_t pointer;
    
    tss_entry_t tss;

} gdt;

extern void gdt_flush (uintptr_t);
#define     ENTRY(x)            (gdt.entries[(x)])

static void gdt_set_gate (uint8_t num, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran) {

    /* Base Address */
    ENTRY (num).base_low = (base & 0xFFFF);
    ENTRY (num).base_middle = (base >> 16) & 0xFF;
    ENTRY (num).base_high = (base >> 24) & 0xFF;
    
    /* Limits */
    ENTRY (num).limit_low = (limit & 0xFFFF);
    ENTRY (num).granularity = (limit >> 16) & 0X0F;
    
    /* Granularity */
    ENTRY (num).granularity |= (gran & 0xF0);
    
    /* Access flags */
    ENTRY (num).access = access;

}

tss_entry_t *g_tss;

void gdt_set_tls (uintptr_t base, uintptr_t limit, uint8_t access, uint8_t flags) {
    gdt_set_gate (6, base, limit, access, flags);
}

static void write_tss (int32_t num, uint16_t ss0, uint32_t esp0) {

    uintptr_t base, limit;
    g_tss = &gdt.tss;
    
    base = (uintptr_t) g_tss;
    limit = sizeof (*g_tss) - 1;
    
    /* Add the TSS descriptor to the GDT. */
    gdt_set_gate (num, base, limit, 0x89, 0x00);
    
    /* Zero out the tss. */
    memset (g_tss, 0, sizeof (*g_tss));
    
    /* Setup. */
    g_tss->ss0 = ss0;
    g_tss->esp0 = esp0;
    
    g_tss->cs = 0x1B;
    g_tss->ss = 0x23;
    g_tss->ds = 0x23;
    g_tss->es = 0x23;
    g_tss->fs = 0x23;
    g_tss->gs = 0x23;
    
    g_tss->iomap_base = sizeof (*g_tss);

}

void gdt_install (void) {

    gdt_pointer_t *gdtp = &gdt.pointer;
    
    gdtp->limit = sizeof (gdt.entries) - 1;
    gdtp->base = (uintptr_t) &ENTRY(0);
    
    /* NULL segment */
    gdt_set_gate (0, 0, 0, 0, 0);
    
    /* Code segment */
    gdt_set_gate (1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    
    /* Data segment */
    gdt_set_gate (2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    
    /* User code */
    gdt_set_gate (3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    
    /* User data */
    gdt_set_gate (4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    
    write_tss (5, 0x10, 0x0);
    gdt_set_gate (6, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    
    /* Flush the GDT. */
    gdt_flush ((uintptr_t) gdtp);
    
    /* Flush the TSS. */
    tss_flush ();

}

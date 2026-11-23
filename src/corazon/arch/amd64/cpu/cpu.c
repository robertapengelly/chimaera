/******************************************************************************
 * @file            cpu.c
 *****************************************************************************/
#include    <corazon/cpu.h>

#include    <stddef.h>
#include    <stdint.h>

extern void syscall_entry (void);

static int cpuid_available = 1;
static int supports_rdtsc  = 0;

void cpu_install (void) {

    uint64_t efer;
    size_t cr4;
    
    /*  CPUID is guaranteed in AMD64 long mode. */
    cpuid_available = 1;
    
    /* Enable global pages. */
    if (cpu_supports (CF_PGE)) {
    
        __asm__ __volatile__ (
            "mov %0, cr4"
            : "=r" (cr4)
        );
        
        cr4 |= (1ULL << 7);
        
        __asm__ __volatile__ (
            "mov cr4, %0"
            : : "r" (cr4)
        );
    
    }
    
    /* Enable FXSAVE/FXRSTOR and SSE state management. */
    if (cpu_supports (CF_FXSR)) {
    
        __asm__ __volatile__ (
            "mov %0, cr4"
            : "=r" (cr4)
        );
        
        cr4 |= (1ULL << 9);
        
        __asm__ __volatile__ (
            "mov cr4, %0"
            : : "r" (cr4)
        );
    
    }

    /* Enable unmasked SIMD floating-point exceptions. */
    if (cpu_supports (CF_SSE)) {
    
        __asm__ __volatile__ (
            "mov %0, cr4"
            : "=r" (cr4)
        );
        
        cr4 |= (1ULL << 10);
        
        __asm__ __volatile__ (
            "mov cr4, %0"
            : : "r" (cr4)
        );
    
    }
    
    efer = cpu_msr_read (0xC0000080);
    efer |= 1;
    
    cpu_msr_write (0xC0000080, efer);
    
    cpu_msr_write (0xC0000082, (uintptr_t) syscall_entry);
    cpu_msr_write (0xC0000084, (1ULL << 9));
    
    cpu_msr_write (0xC0000081, ((uint64_t) 0x1B << 48) | ((uint64_t) 0x08 << 32));
    supports_rdtsc = cpu_supports (CF_RDTSC);

}

int cpu_rdtsc_available (void) {
    return supports_rdtsc;
}

int cpu_supports (CPU_FEATURE feature) {

    CPU_REGISTER r;

    if (feature == CF_CPUID) {
        return cpuid_available;
    }
    
    r = feature & ~31;
    return ((cpu_id_get_register (0x00000001, r) & (1U << (feature - r))) != 0);

}

uint32_t cpu_id_get_register (uint32_t function, CPU_REGISTER reg) {

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    
    __asm__ __volatile__ (
        "cpuid"
        : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
        : "a" (function), "c" (0)
    );
    
    switch (reg) {
    
        case CR_EAX:
        
            return (eax);
        
        case CR_EBX:
        
            return (ebx);
        
        case CR_ECX:
        
            return (ecx);
        
        case CR_EDX:
        
            return (edx);
        
        default:
        
            return (0);
    
    }

}

uint64_t cpu_msr_read (uint32_t msr) {

    uint32_t low;
    uint32_t high;
    
    __asm__ __volatile__ (
        "rdmsr"
        : "=a" (low), "=d" (high)
        : "c" (msr)
    );
    
    return (((uint64_t) high << 32) | low);

}

void cpu_msr_write (uint32_t msr, uint64_t value) {

    uint32_t low;
    uint32_t high;
    
    low  = (uint32_t) value;
    high = (uint32_t) (value >> 32);
    
    __asm__ __volatile__ (
        "wrmsr"
        :
        : "a" (low), "c" (msr), "d" (high)
    );

}

void fpu_install (void) {

    uint32_t mxcsr = 0x1F80;
    
    size_t cr0;
    size_t cr4;
    
    __asm__ __volatile__ ("clts");
    
    __asm__ __volatile__ (
        "mov %0, cr0"
        : "=r" (cr0)
    );
    
    cr0 &= ~(1ULL << 2);       /* Clear EM. */
    cr0 |=  (1ULL << 1);       /* Set MP. */
    
    __asm__ __volatile__ (
        "mov cr0, %0"
        :
        : "r" (cr0)
    );
    
    __asm__ __volatile__ ("fninit");
    
    __asm__ __volatile__ (
        "mov %0, cr4"
        : "=r" (cr4)
    );
    
    cr4 |= (1ULL << 9);        /* OSFXSR. */
    cr4 |= (1ULL << 10);       /* OSXMMEXCPT. */
    
    __asm__ __volatile__ (
        "mov cr4, %0"
        :
        : "r" (cr4)
    );
    
    __asm__ __volatile__ (
        "ldmxcsr %0"
        :
        : "m" (mxcsr)
    );

}

void pat_install (void) {

    uint64_t pat;
    
    if (!cpu_supports (CF_PAT)) {
        return;
    }
    
    pat = cpu_msr_read (0x277);
    
    pat |=  (1ULL << 56);
    pat &= ~(3ULL << 57);
    
    cpu_msr_write (0x277, pat);

}

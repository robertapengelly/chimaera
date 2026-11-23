/******************************************************************************
 * @file            timer.c
 *****************************************************************************/
#include    <corazon/tasking/irq.h>
#include    <corazon/cpu.h>
#include    <corazon/regs.h>
#include    <corazon/spin.h>
#include    <corazon/timer.h>

#include    <stdint.h>

#define     SUBSECONDS_PER_SECOND       1000000

#define     PIT_COMMAND                 0x43
#define     PIT_CHANNEL2                0x42
#define     PIT_SPEAKER                 0x61

#define     PIT_FREQUENCY               1193182

static uint16_t systemfrequency = 0;
static uint64_t timer_ticks = 0;

static uint64_t tsc_basis_time = 0;
uint64_t tsc_mhz = 0;

extern uint64_t read_tsc (void);
extern int cpu_rdtsc_available (void);

static void update_ticks (uint64_t ticks, uint64_t *timer_ticks, uint64_t *timer_subticks) {

    *timer_subticks = ticks - tsc_basis_time;
    *timer_ticks = *timer_subticks / SUBSECONDS_PER_SECOND;
    *timer_subticks = *timer_subticks % SUBSECONDS_PER_SECOND;

}

static void arch_calibrate (void) {

    uint32_t max_leaf;
    uint32_t denominator;
    uint32_t numerator;
    uint32_t crystal;
    
    uint32_t attempts;
    uint8_t status;
    
    uint64_t start, end, usec;
    uint64_t tsc_hz;
    
    if (!cpu_rdtsc_available ()) {
        return;
    }
    
    /**
     * Prefer CPUID 0x15 when the processor provides a complete
     * TSC/crystal-clock relationship.
     */
    max_leaf = cpu_id_get_register (0x00000000, CR_EAX);
    
    if (max_leaf >= 0x00000015) {
    
        denominator = cpu_id_get_register (0x00000015, CR_EAX);
        numerator   = cpu_id_get_register (0x00000015, CR_EBX);
        crystal     = cpu_id_get_register (0x00000015, CR_ECX);
        
        if (denominator && numerator && crystal) {
        
            tsc_hz = ((uint64_t) crystal * numerator) / denominator;
            tsc_mhz = tsc_hz / 1000000ULL;
            
            return;
        
        }
    
    }
    
    /* Fall back to PIT channel 2 calibration. */
    /* Disable the speaker and hold channel 2's gate low. */
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "Nd" ((uint16_t) PIT_SPEAKER)
    );
    
    status &= 0xFC;
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (status), "Nd" ((uint16_t) PIT_SPEAKER)
    );
    
    /**
     * Channel 2, mode 0 (interrupt on terminal count),
     * binary, lo/hi byte.
     *
     * OUT2 is low until the terminal count is reached, then high.
     * Counter = 0x2E9B = 11931 ticks, approximately 10 ms.
     */
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" ((uint8_t) 0xB0), "Nd" ((uint16_t) PIT_COMMAND)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" ((uint8_t) 0x9B), "Nd" ((uint16_t) PIT_CHANNEL2)
    );
    
    /* Preserve the delay used by the original implementation. */
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "Nd" ((uint16_t) 0x60)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" ((uint8_t) 0x2E), "Nd" ((uint16_t) PIT_CHANNEL2)
    );
    
    /* The mode 0 output must be low before counting starts. */
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (status)
        : "Nd" ((uint16_t) PIT_SPEAKER)
    );
    
    if (status & 0x20) {
        return;
    }
    
    /* Raise the gate to start the counter. */
    status |= 0x01;
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (status), "Nd" ((uint16_t) PIT_SPEAKER)
    );
    
    start = read_tsc ();
    
    /* Do not hang forever if OUT2 never reaches terminal count. */
    for (attempts = 0; attempts < 1000000; attempts++) {
    
        __asm__ __volatile__ (
            "inb %1, %0"
            : "=a" (status)
            : "Nd" ((uint16_t) PIT_SPEAKER)
        );
        
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("nop");
#endif
        
        if (status & 0x20) {
            break;
        }
    
    }
    
    if (!(status & 0x20)) {
        return;
    }
    
    end = read_tsc ();
    
    /* 0x2E9B PIT ticks at 1.193182 MHz. */
    usec = (11931ULL * 1000000ULL) / PIT_FREQUENCY;
    tsc_mhz = (end - start) / usec;

}

uint64_t arch_get_ticks (void) {

    if (cpu_rdtsc_available () && tsc_mhz) {
        return (read_tsc () / tsc_mhz) - tsc_basis_time;
    }

    return timer_ticks;

}

static spin_lock_t clock_lock = { 0 };

static void arch_update_clock (void) {

    uint64_t clock_ticks, ticks, subticks;
    
    if (!cpu_rdtsc_available () || !tsc_mhz) {
        return;
    }
    
    spin_lock (&clock_lock);
    
    clock_ticks = read_tsc () / tsc_mhz;
    update_ticks (clock_ticks, &ticks, &subticks);
    
    spin_unlock (&clock_lock);
    
    /* wakeup_sleepers(ticks, subticks); */

}

extern void schedule (struct regs *regs);

static int timer_handler (struct regs *r) {

    if (cpu_rdtsc_available ()) {
        arch_update_clock ();
    }
    
    timer_ticks++;
    
    schedule (r);
    return 0;

}

static void timer_set_frequency (uint16_t freq) {

    uint16_t divisor;
    
    systemfrequency = freq;
    divisor = TIMECOUNTER_i8254_FREQU / systemfrequency;
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" ((uint8_t) (COUNTER_0 | RW_LO_HI_MODE | RATEGENERATOR)), "Nd" ((uint16_t) COMMANDREGISTER)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" ((uint8_t) (divisor & 0xFF)), "Nd" ((uint16_t) COUNTER_0_DATAPORT)
    );
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" ((uint8_t) ((divisor >> 8) & 0xFF)), "Nd" ((uint16_t) COUNTER_0_DATAPORT)
    );

}

void timer_install (uint16_t sysfreq) {

    timer_set_frequency (sysfreq);
    
    if (cpu_rdtsc_available ()) {
    
        arch_calibrate ();
        
        if (tsc_mhz) {
            tsc_basis_time = read_tsc () / tsc_mhz;
        }
    
    }
    
    irq_install_handler (0, timer_handler, "timer");

}

uint64_t get_uptime_seconds (void) {

    if (cpu_rdtsc_available () && tsc_mhz) {
        return arch_get_ticks () / SUBSECONDS_PER_SECOND;
    }
    
    return timer_ticks / systemfrequency;

}

uint64_t get_uptime_microseconds (void) {

    if (cpu_rdtsc_available () && tsc_mhz) {
        return arch_get_ticks ();
    }
    
    return (timer_ticks * 1000000ULL) / systemfrequency;

}

uint64_t get_uptime_milliseconds (void) {

    if (cpu_rdtsc_available () && tsc_mhz) {
        return arch_get_ticks () / 1000;
    }

    return (timer_ticks * 1000) / systemfrequency;

}

static uint32_t timer_milliseconds_to_ticks (uint32_t milliseconds) {
    return (milliseconds * systemfrequency) / 1000;
}

void delay (uint32_t ms) {

    if (cpu_rdtsc_available () && tsc_mhz) {
    
        uint64_t target = arch_get_ticks () + ((uint64_t) ms * 1000);
        
        while (arch_get_ticks () < target) {
        
#if     defined (__AMD64__)
            __asm__ __volatile__ ("pause");
#else
            __asm__ __volatile__ ("hlt");
#endif
        
        }
    
    } else {
    
        uint64_t target = timer_ticks + timer_milliseconds_to_ticks (ms);
        while (timer_ticks < target) { __asm__ __volatile__ ("hlt"); }
    
    }

}

/******************************************************************************
 * @file            cmos.c
 *****************************************************************************/
#include    <corazon/spin.h>
#include    <stdint.h>

static spin_lock_t tmp_cmos_lock = { 0 };

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

/* Read byte from CMOS. */
uint8_t cmos_read (uint8_t offset) {

    uintptr_t flags = save_interrupts ();
    uint8_t tmp, ret;
    
    __asm__ __volatile__ ("cli");
    spin_lock (&tmp_cmos_lock);
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (tmp)
        : "Nd" (0x70)
    );
    
    tmp &= 1U << 7; 
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (tmp | (offset & 0x7f)), "Nd" (0x70)
    );
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (ret)
        : "Nd" (0x71)
    );
    
    spin_unlock (&tmp_cmos_lock);
    restore_interrupts (flags);
    
    return ret;

}

/* Read the RTC as seconds since 1970-01-01 00:00:00 UTC. */
int cmos_get_unix_time (uint64_t *seconds) {

    static const uint8_t days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    
    uint8_t second, minute, hour, day, month, year, century, status;
    uint32_t full_year, days = 0, y, m, attempts;
    
    int pm, leap;
    
    if (!seconds) {
        return 0;
    }
    
    for (attempts = 0; attempts < 100000; attempts++) {
    
        if (cmos_read (0x0A) & 0x80) {
            continue;
        }
        
        second = cmos_read (0x00);
        minute = cmos_read (0x02);
        hour = cmos_read (0x04);
        day = cmos_read (0x07);
        month = cmos_read (0x08);
        year = cmos_read (0x09);
        century = cmos_read (0x32);
        status = cmos_read (0x0B);
        
        if (!(cmos_read (0x0A) & 0x80) && cmos_read (0x00) == second) {
            break;
        }
    
    }
    
    if (attempts == 100000) {
        return 0;
    }
    
    pm = (hour & 0x80) != 0;
    hour &= 0x7F;
    
    if (!(status & 0x04)) {
    
        second = (second & 0x0F) + ((second >> 4) * 10);
        minute = (minute & 0x0F) + ((minute >> 4) * 10);
        hour = (hour & 0x0F) + ((hour >> 4) * 10);
        day = (day & 0x0F) + ((day >> 4) * 10);
        month = (month & 0x0F) + ((month >> 4) * 10);
        year = (year & 0x0F) + ((year >> 4) * 10);
        century = (century & 0x0F) + ((century >> 4) * 10);
    
    }
    
    if (!(status & 0x02)) {
    
        if (hour < 1 || hour > 12) {
            return 0;
        }
        
        hour = (hour % 12) + (pm ? 12 : 0);
    
    }
    
    if (century >= 19 && century <= 99 && (century != 19 || year >= 70)) {
        full_year = (uint32_t) century * 100 + year;
    } else {
        full_year = (year >= 70 ? 1900 : 2000) + year;
    }
    
    leap = (full_year % 4 == 0 && (full_year % 100 != 0 || full_year % 400 == 0));
    
    if (full_year < 1970 || month < 1 || month > 12 || day < 1 ||
        day > days_in_month[month - 1] + (month == 2 && leap) ||
            hour > 23 || minute > 59 || second > 59) {
        return 0;
    }
    
    for (y = 1970; y < full_year; y++) {
        days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    }
    
    for (m = 1; m < month; m++) {
        days += days_in_month[m - 1] + (m == 2 && leap);
    }
    
    days += day - 1;
    
    *seconds = (uint64_t) days * 86400ULL + (uint64_t) hour * 3600 + (uint64_t) minute * 60 + second;
    return 1;

}

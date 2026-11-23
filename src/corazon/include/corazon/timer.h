/******************************************************************************
 * @file            timer.h
 *****************************************************************************/
#ifndef     _TIMER_H
#define     _TIMER_H

#include    <stdint.h>

void timer_install (uint16_t sysfreq);
void delay (uint32_t microsec);

#define     TIMECOUNTER_i8254_FREQU     1193182
#define     COUNTER_0_DATAPORT          0x40                /* Channel 0 data port (read/write) */
#define     COMMANDREGISTER             0x43                /* Command register (write only, read ignored) */
#define     RATEGENERATOR               0x04                /* Mode 2: divide by N counter */
#define     RW_LO_HI_MODE               0x30                /* 2xRead/2xWrite bits 0..7 then 8..15 of counter value */
#define     COUNTER_0                   0x00                /* select counter 0 */

uint64_t get_uptime_microseconds (void);
uint64_t get_uptime_milliseconds (void);
uint64_t get_uptime_seconds (void);

#endif      /* _TIMER_H */

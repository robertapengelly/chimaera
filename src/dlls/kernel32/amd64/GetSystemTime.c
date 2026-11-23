/******************************************************************************
 * @file            GetSystemTime.c
 *****************************************************************************/
#include    <stdint.h>

typedef struct {

    uint16_t wYear;
    uint16_t wMonth;
    uint16_t wDayOfWeek;
    uint16_t wDay;
    uint16_t wHour;
    uint16_t wMinute;
    uint16_t wSecond;
    uint16_t wMilliseconds;

} SYSTEMTIME;

__attribute__ ((dllimport, ms_abi)) uint32_t ZwQuerySystemTime (uint64_t *system_time);

static int get_unix_seconds (uint32_t *seconds) {

    uintptr_t filetime;
    
    if (ZwQuerySystemTime (&filetime) != 0 || filetime < 116444736000000000ULL) {
        return 0;
    }
    
    *seconds = (uint32_t) ((filetime - 116444736000000000ULL) / 10000000ULL);
    return 1;

}

static void fill_system_time (SYSTEMTIME *system_time) {

    static const uint8_t days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    
    uint32_t seconds, days, year, month, days_in_year, month_days;
    int leap;
    
    if (!system_time || !get_unix_seconds (&seconds)) {
        return;
    }
    
    system_time->wSecond = (uint16_t) (seconds % 60);
    seconds /= 60;
    
    system_time->wMinute = (uint16_t) (seconds % 60);
    seconds /= 60;
    
    system_time->wHour = (uint16_t) (seconds % 24);
    days = seconds / 24;
    
    system_time->wDayOfWeek = (uint16_t) ((days + 4) % 7);
    
    for (year = 1970;; year++) {
    
        days_in_year = 365 + (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        
        if (days < days_in_year) {
            break;
        }
        
        days -= days_in_year;
    
    }
    
    leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    
    for (month = 1; month <= 12; month++) {
    
        month_days = days_in_month[month - 1] + (month == 2 && leap);
        
        if (days < month_days) {
            break;
        }
        
        days -= month_days;
    
    }
    
    system_time->wYear = (uint16_t) year;
    system_time->wMonth = (uint16_t) month;
    system_time->wDay = (uint16_t) (days + 1);
    system_time->wMilliseconds = 0;

}

#if     defined (__AMD64__)
__attribute__ ((dllexport, ms_abi)) void GetSystemTime (SYSTEMTIME *system_time) {
#else
__attribute__ ((dllexport, stdcall)) void GetSystemTime (SYSTEMTIME *system_time) {
#endif
    fill_system_time (system_time);
}

#if     defined (__AMD64__)
__attribute__ ((dllexport, ms_abi)) void GetLocalTime (SYSTEMTIME *system_time) {
#else
__attribute__ ((dllexport, stdcall)) void GetLocalTime (SYSTEMTIME *system_time) {
#endif
    fill_system_time (system_time);
}

/******************************************************************************
 * @file            mktime.c
 *****************************************************************************/
#include    <errno.h>
#include    <limits.h>
#include    <stdint.h>
#include    <time.h>

static int64_t days_from_civil (int64_t year, int64_t month, int64_t day) {

    int64_t era, yoe, doy, doe;
    year -= (month <= 2);
    
    era = (year >= 0 ? year : year - 399) / 400;
    yoe = year - era * 400;
    
    doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    
    return era * 146097 + doe - 719468;

}

static void civil_from_days (int64_t days, int64_t *year, int *month, int *day) {

    int64_t era, doe, yoe, doy, mp;
    days += 719468;
    
    era = (days >= 0 ? days : days - 146096) / 146097;
    doe = days - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    
    *year = yoe + era * 400;
    
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    
    *day = (int) (doy - (153 * mp + 2) / 5 + 1);
    *month = (int) (mp + (mp < 10 ? 3 : -9));
    *year += (*month <= 2);

}

time_t mktime (struct tm *timeptr) {

    int64_t year, month, days, seconds, value;
    
    struct tm result;
    int normalized_month, normalized_day;
    
    if (!timeptr) {
    
        errno = EINVAL;
        return (time_t) -1;
    
    }
    
    month = (int64_t) timeptr->tm_mon;
    year = (int64_t) timeptr->tm_year + 1900 + month / 12;
    
    month %= 12;
    
    if (month < 0) {
    
        month += 12;
        year--;
    
    }
    
    days = days_from_civil (year, month + 1, 1) + (int64_t) timeptr->tm_mday - 1;
    seconds = (int64_t) timeptr->tm_hour * 3600 + (int64_t) timeptr->tm_min * 60 + (int64_t) timeptr->tm_sec;
    
    days += seconds / 86400;
    seconds %= 86400;
    
    if (seconds < 0) {
    
        seconds += 86400;
        days--;
    
    }
    
    value = days * 86400 + seconds;
    
#ifndef     __AMD64__
    if (value < -2147483647LL - 1 || value > 2147483647LL) {
    
        errno = EOVERFLOW;
        return (time_t) -1;
    
    }
#endif
    
    civil_from_days (days, &year, &normalized_month, &normalized_day);
    
    if (year - 1900 < INT_MIN || year - 1900 > INT_MAX) {
    
        errno = EOVERFLOW;
        return (time_t) -1;
    
    }
    
    result = *timeptr;
    
    result.tm_sec = (int) (seconds % 60);
    result.tm_min = (int) ((seconds / 60) % 60);
    result.tm_hour = (int) (seconds / 3600);
    result.tm_mday = normalized_day;
    result.tm_mon = normalized_month - 1;
    result.tm_year = (int) (year - 1900);
    result.tm_wday = (int) ((days + 4) % 7);
    
    if (result.tm_wday < 0) {
        result.tm_wday += 7;
    }
    
    result.tm_yday = (int) (days - days_from_civil (year, 1, 1));
    result.tm_isdst = 0;
    
    *timeptr = result;
    return (time_t) value;

}

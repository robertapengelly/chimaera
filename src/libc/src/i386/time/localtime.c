/******************************************************************************
 * @file            localtime.c
 *****************************************************************************/
#include    <errno.h>
#include    <limits.h>
#include    <stdint.h>
#include    <time.h>

static struct tm result;

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

struct tm *localtime (const time_t *timer) {

    static const int month_days[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    
    int64_t days, seconds, year;
    int month, day, leap;
    
    if (!timer) {
    
        errno = EINVAL;
        return 0;
    
    }
    
    days = (int64_t) *timer / 86400;
    seconds = (int64_t) *timer % 86400;
    
    if (seconds < 0) {
    
        seconds += 86400;
        days--;
    
    }
    
    civil_from_days (days, &year, &month, &day);
    
    if (year - 1900 < INT_MIN || year - 1900 > INT_MAX) {
    
        errno = EOVERFLOW;
        return 0;
    
    }
    
    leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    
    result.tm_sec = (int) (seconds % 60);
    result.tm_min = (int) ((seconds / 60) % 60);
    result.tm_hour = (int) (seconds / 3600);
    result.tm_mday = day;
    result.tm_mon = month - 1;
    result.tm_year = (int) (year - 1900);
    result.tm_wday = (int) ((days + 4) % 7);
    
    if (result.tm_wday < 0) {
        result.tm_wday += 7;
    }
    
    result.tm_yday = month_days[month - 1] + day - 1;
    
    if (leap && month > 2) {
        result.tm_yday++;
    }
    
    result.tm_isdst = 0;
    return &result;

}

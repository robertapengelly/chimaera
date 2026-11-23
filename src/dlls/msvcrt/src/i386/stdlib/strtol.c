/******************************************************************************
 * @file            strtol.c
 *****************************************************************************/
#include    <ctype.h>
#include    <errno.h>
#include    <limits.h>
#include    <stdlib.h>

__attribute__ ((dllexport)) long strtol (const char *nptr, char **endptr, int base) {

    const char *p = nptr;
    unsigned long value = 0, limit, cutoff, cutlim;
    int negative = 0, digits = 0, overflow = 0, digit;
    
    if (endptr) {
        *endptr = (char *) nptr;
    }
    
    if (base != 0 && (base < 2 || base > 36)) {
    
        errno = EINVAL;
        return 0;
    
    }
    
    while (isspace ((int) (unsigned char) *p)) {
        p++;
    }
    
    if (*p == '-' || *p == '+') {
    
        negative = (*p == '-');
        p++;
    
    }
    
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit ((int) (unsigned char) p[2])) {
    
        base = 16;
        p += 2;
    
    } else if (base == 0) {
        base = (*p == '0') ? 8 : 10;
    }
    
    limit = (unsigned long) LONG_MAX;
    
    if (negative) {
        limit++;
    }
    
    cutoff = limit / (unsigned long) base;
    cutlim = limit % (unsigned long) base;
    
    for (; *p; p++) {
    
        if (*p >= '0' && *p <= '9') {
            digit = *p - '0';
        } else if (*p >= 'a' && *p <= 'z') {
            digit = *p - 'a' + 10;
        } else if (*p >= 'A' && *p <= 'Z') {
            digit = *p - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) {
            break;
        }
        
        digits = 1;
        
        if (value > cutoff || (value == cutoff && (unsigned long) digit > cutlim)) {
            overflow = 1;
        } else if (!overflow) {
            value = value * (unsigned long) base + (unsigned long) digit;
        }
    
    }
    
    if (!digits) {
        return 0;
    }
    
    if (endptr) {
        *endptr = (char *) p;
    }
    
    if (overflow) {
    
        errno = ERANGE;
        return negative ? LONG_MIN : LONG_MAX;
    
    }
    
    if (negative) {
    
        if (value == limit) {
            return LONG_MIN;
        }
        
        return -(long) value;
    
    }
    
    return (long) value;

}

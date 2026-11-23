/******************************************************************************
 * @file            strftime.c
 *****************************************************************************/
#include    <stdint.h>
#include    <time.h>

static const char *weekdays[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

static const char *months[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static int append_char (char *s, size_t maxsize, size_t *count, char c) {

    if (*count >= maxsize - 1) {
        return -1;
    }
    
    s[(*count)++] = c;
    return 0;

}

static int append_text (char *s, size_t maxsize, size_t *count, const char *text) {

    while (*text) {
    
        if (append_char (s, maxsize, count, *text++) < 0) {
            return -1;
        }
    
    }
    
    return 0;

}

static int append_number (char *s, size_t maxsize, size_t *count, int64_t value, int width, char pad) {

    char digits[24];
    
    uint64_t number;
    int length = 0;
    
    if (value < 0) {
        number = (uint64_t) (-(value + 1)) + 1;
    } else {
        number = (uint64_t) value;
    }
    
    do {
    
        digits[length++] = (char) ('0' + number % 10);
        number /= 10;
    
    } while (number);
    
    if (value < 0 && append_char (s, maxsize, count, '-') < 0) {
        return -1;
    }
    
    while (length < width) {
    
        if (append_char (s, maxsize, count, pad) < 0) {
            return -1;
        }
        
        width--;
    
    }
    
    while (length) {
    
        if (append_char (s, maxsize, count, digits[--length]) < 0) {
            return -1;
        }
    
    }
    
    return 0;

}

static int format_time (char *s, size_t maxsize, size_t *count, const char *format, const struct tm *timeptr) {

    int64_t year = (int64_t) timeptr->tm_year + 1900;
    
    int week;
    int hour;
    
    char spec;
    
    while (*format) {
    
        if (*format != '%') {
        
            if (append_char (s, maxsize, count, *format++) < 0) {
                return -1;
            }
            
            continue;
        
        }
        
        format++;
        spec = *format;
        
        if (!spec) {
            break;
        }
        
        format++;
        
        switch (spec) {
        
            case 'a':
            case 'A':
            
                if (timeptr->tm_wday < 0 || timeptr->tm_wday > 6) {
                    return -1;
                }
                
                if (append_text (s, maxsize, count, spec == 'a' ? "" : weekdays[timeptr->tm_wday]) < 0) {
                    return -1;
                }
                
                if (spec == 'a') {
                
                    const char *p = weekdays[timeptr->tm_wday];
                    int i;
                    
                    for (i = 0; i < 3; i++) {
                    
                        if (append_char (s, maxsize, count, p[i]) < 0) {
                            return -1;
                        }
                    
                    }
                
                }
                
                break;
            
            case 'b':
            case 'B':
            
                if (timeptr->tm_mon < 0 || timeptr->tm_mon > 11) {
                    return -1;
                }
                
                if (spec == 'B') {
                
                    if (append_text (s, maxsize, count, months[timeptr->tm_mon]) < 0) {
                        return -1;
                    }
                
                } else {
                
                    const char *p = months[timeptr->tm_mon];
                    int i;
                    
                    for (i = 0; i < 3; i++) {
                    
                        if (append_char (s, maxsize, count, p[i]) < 0) {
                            return -1;
                        }
                    
                    }
                
                }
                
                break;
            
            case 'c':
            
                if (format_time (s, maxsize, count, "%a %b %e %H:%M:%S %Y", timeptr) < 0) {
                    return -1;
                }
                
                break;
            
            case 'd':
            case 'e':
            
                if (append_number (s, maxsize, count, timeptr->tm_mday, 2, spec == 'e' ? ' ' : '0') < 0) {
                    return -1;
                }
                
                break;
            
            case 'H':
            case 'I':
            
                hour = timeptr->tm_hour;
                
                if (spec == 'I') {
                
                    hour %= 12;
                    
                    if (!hour) {
                        hour = 12;
                    }
                
                }
                
                if (append_number (s, maxsize, count, hour, 2, '0') < 0) {
                    return -1;
                }
                
                break;
            
            case 'j':
            
                if (append_number (s, maxsize, count, timeptr->tm_yday + 1, 3, '0') < 0) {
                    return -1;
                }
                
                break;
            
            case 'm':
            
                if (append_number (s, maxsize, count, timeptr->tm_mon + 1, 2, '0') < 0) {
                    return -1;
                }
                
                break;
            
            case 'M':
            
                if (append_number (s, maxsize, count, timeptr->tm_min, 2, '0') < 0) {
                    return -1;
                }
                
                break;
            
            case 'p':
            
                if (append_text (s, maxsize, count, timeptr->tm_hour < 12 ? "AM" : "PM") < 0) {
                    return -1;
                }
                
                break;
            
            case 'S':
            
                if (append_number (s, maxsize, count, timeptr->tm_sec, 2, '0') < 0) {
                    return -1;
                }
                
                break;
            
            case 'U':
            case 'W':
            
                week = spec == 'U' ? timeptr->tm_wday : (timeptr->tm_wday + 6) % 7;
                week = (timeptr->tm_yday + 7 - week) / 7;
                
                if (append_number (s, maxsize, count, week, 2, '0') < 0) {
                    return -1;
                }
                
                break;
            
            case 'w':
            
                if (append_number (s, maxsize, count, timeptr->tm_wday, 1, '0') < 0) {
                    return -1;
                }
                
                break;
            
            case 'x':
            
                if (format_time (s, maxsize, count, "%m/%d/%y", timeptr) < 0) {
                    return -1;
                }
                
                break;
            
            case 'X':
            
                if (format_time (s, maxsize, count, "%H:%M:%S", timeptr) < 0) {
                    return -1;
                }
                
                break;
            
            case 'y':
            
                if (append_number (s, maxsize, count, (year % 100 + 100) % 100, 2, '0') < 0) {
                    return -1;
                }
                
                break;
            
            case 'Y':
            
                if (append_number (s, maxsize, count, year, 4, '0') < 0) {
                    return -1;
                }
                
                break;
            
            case 'Z':
            
                if (append_text (s, maxsize, count, "UTC") < 0) {
                    return -1;
                }
                
                break;
            
            case '%':
            
                if (append_char (s, maxsize, count, '%') < 0) {
                    return -1;
                }
                
                break;
            
            default:
            
                if (append_char (s, maxsize, count, '%') < 0 || append_char (s, maxsize, count, spec) < 0) {
                    return -1;
                }
                
                break;
        
        }
    
    }
    
    return 0;

}

__attribute__ ((dllexport)) size_t strftime (char *s, size_t maxsize, const char *format, const struct tm *timeptr) {

    size_t count = 0;
    
    if (!maxsize) {
        return 0;
    }
    
    if (format_time (s, maxsize, &count, format, timeptr) < 0) {
    
        s[count] = '\0';
        return 0;
    
    }
    
    s[count] = '\0';
    return count;

}

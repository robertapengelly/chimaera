/******************************************************************************
 * @file            xvasprintf.c
 *****************************************************************************/
#include    <stdarg.h>
#include    <stddef.h>
#include    <stdint.h>
#include    <string.h>

static uint64_t get_number (const char **s) {

    const char *p = *s;
    uint64_t res = 0;
    
    while (*p >= '0' && *p <= '9') {
    
        res = (res * 10) + (*p - '0');
        ++p;
    
    }
    
    *s = p;
    return res;

}

static uint64_t integer_to_string (char *out, uint64_t value, uint64_t base, const char *digits) {

    uint64_t temp = value, count = 1, i;
    
    while (temp >= base) {
    
        temp /= base;
        count++;
    
    }
    
    for (i = count; i > 0; i--) {
    
        out[i - 1] = digits[value % base];
        value /= base;
    
    }
    
    return count;

}

#define     LENGTH_DEFAULT              0
#define     LENGTH_hh                   1
#define     LENGTH_h                    2
#define     LENGTH_l                    3
#define     LENGTH_ll                   4
#define     LENGTH_j                    5
#define     LENGTH_z                    6
#define     LENGTH_t                    7
#define     LENGTH_L                    8

#define     STEP                        if (!*++s) { return written; }

__attribute__ ((dllexport)) int xvasprintf (int (*callback) (void *, char), void *user_data, const char *fmt, va_list ap) {

    const char *s = fmt;
    int written = 0;
    
    char number_buffer[24];
    char precision_buffer[24];
    
    int flag_left_justify = 0;
    int flag_always_prepend_sign = 0;
    int flag_always_prepend_space_plussign = 0;
    int flag_force_0x_or_dec = 0;
    int flag_left_pad_zeroes = 0;
    
    int explicit_precision, specifier;
    int flag_found, length;
    
    const char *digits;
    uint64_t value;
    
    int additionals_len = 0;
    int is_negative, is_signed, base;
    
    uint64_t value_integer, value_fractional, width, len;
    uint64_t len_int, len_fract, precision, i;
    
    double value_fractional_d, dvalue;
    int ret;
    
    while (*s) {
    
        if (*s == '%') {
        
            STEP;
            
            if (*s == '%') {
            
                if ((ret = callback (user_data, *s)) <= 0) {
                    return ret;
                }
                
                ++written;
                
                STEP;
                continue;
            
            }
            
            flag_always_prepend_space_plussign = 0;
            flag_always_prepend_sign = 0;
            
            flag_force_0x_or_dec = 0;
            flag_left_pad_zeroes = 0;
            
            flag_left_justify = 0;
            
            do {
            
                flag_found = 0;
                
                switch (*s) {
                
                    case '-':
                    
                        flag_left_justify = 1;
                        flag_found = 1;
                        
                        break;
                    
                    case '+':
                    
                        flag_always_prepend_sign = 1;
                        flag_found = 1;
                        
                        break;
                    
                    case ' ':
                    
                        flag_always_prepend_space_plussign = 1;
                        flag_found = 1;
                        
                        break;
                    
                    case '#':
                    
                        flag_force_0x_or_dec = 1;
                        flag_found = 1;
                        
                        break;
                    
                    case '0':
                    
                        flag_left_pad_zeroes = 1;
                        flag_found = 1;
                        
                        break;
                
                }
                
                if (flag_found) {
                    STEP;
                }
            
            } while (flag_found);
            
            width = 0;
            
            if (*s == '*') {
            
                width = va_arg (ap, int);
                STEP;
            
            } else if (*s >= '0' && *s <= '9') {
                width = get_number (&s);
            }
            
            explicit_precision = 0;
            precision = 6;
            
            if (*s == '.') {
            
                explicit_precision = 1;
                STEP;
                
                if (*s == '*') {
                
                    precision = va_arg (ap, int);
                    STEP;
                
                } else if (*s >= '0' && *s <= '9') {
                    precision = get_number (&s);
                }
            
            }
            
            length = LENGTH_DEFAULT;
            
            if (*s == 'h') {
            
                length = LENGTH_h;
                STEP;
                
                if (*s == 'h') {
                
                    length = LENGTH_hh;
                    STEP;
                
                }
            
            } else if (*s == 'l') {
            
                length = LENGTH_l;
                STEP;
                
                if (*s == 'l') {
                
                    length = LENGTH_ll;
                    STEP;
                
                }
            
            } else if (*s == 'j') {
            
                length = LENGTH_j;
                STEP;
            
            } else if (*s == 'z') {
            
                length = LENGTH_z;
                STEP;
            
            } else if (*s == 't') {
            
                length = LENGTH_t;
                STEP;
            
            } else if (*s == 'L') {
            
                length = LENGTH_L;
                STEP;
            
            }
            
            specifier = *s;
            
            is_signed = 0;
            value = 0;
            
            additionals_len = 0;
            
            switch (*s) {
            
                case 'u':       case 'o':       case 'x':       case 'X':
                case 'd':       case 'i':       case 'p':       case 'P':
                
                    if (specifier == 'd' || specifier == 'i') {
                    
                        is_signed = 1;
                        
                        switch (length) {
                        
                            case LENGTH_DEFAULT:
                            
                                value = va_arg (ap, int);
                                break;
                            
                            case LENGTH_hh:
                            
                                value = (signed char) va_arg (ap, int);
                                break;
                            
                            case LENGTH_h:
                            
                                value = (short int) va_arg (ap, int);
                                break;
                            
                            case LENGTH_l:
                            
                                value = va_arg (ap, long int);
                                break;
                            
                            case LENGTH_ll:
                            
                                value = va_arg (ap, long long int);
                                break;
                            
                            case LENGTH_j:
                            
                                value = va_arg (ap, intmax_t);
                                break;
                            
                            case LENGTH_z:
                            
                                value = va_arg (ap, size_t);
                                break;
                            
                            case LENGTH_t:
                            
                                value = va_arg (ap, ptrdiff_t);
                                break;
                            
                            default:
                            
                                return -1;
                        
                        }
                    
                    } else {
                    
                        is_signed = 0;
                        
                        switch (length) {
                        
                            case LENGTH_DEFAULT:
                            
                                value = va_arg (ap, unsigned int);
                                break;
                            
                            case LENGTH_hh:
                            
                                value = (signed char) va_arg (ap, int);
                                break;
                            
                            case LENGTH_h:
                            
                                value = (short int) va_arg (ap, int);
                                break;
                            
                            case LENGTH_l:
                            
                                value = va_arg (ap, unsigned long int);
                                break;
                            
                            case LENGTH_ll:
                            
                                value = va_arg (ap, unsigned long long int);
                                break;
                            
                            case LENGTH_j:
                            
                                value = va_arg (ap, uintmax_t);
                                break;
                            
                            case LENGTH_z:
                            
                                value = va_arg (ap, size_t);
                                break;
                            
                            case LENGTH_t:
                            
                                value = va_arg (ap, ptrdiff_t);
                                break;
                            
                            default:
                            
                                return -1;
                        
                        }
                    
                    }
                    
                    is_negative = (is_signed && ((int64_t) value) < 0);
                    
                    if (specifier == 'p') {
                    
                        specifier = 'x';
                        flag_force_0x_or_dec = 1;
                    
                    } else if (specifier == 'P') {
                    
                        specifier = 'X';
                        flag_force_0x_or_dec = 1;
                    
                    }
                    
                    if (specifier == 'x') {
                    
                        digits = "0123456789abcdef";
                        base = 16;
                    
                    } else if (specifier == 'X') {
                    
                        digits = "0123456789ABCDEF";
                        base = 16;
                    
                    } else if (specifier == 'o') {
                    
                        digits = "012345678";
                        base = 8;
                    
                    } else {
                    
                        digits = "0123456789";
                        base = 10;
                    
                    }
                    
                    len = integer_to_string (number_buffer, (is_negative ? -((int64_t) value) : value), base, digits);
                    
                    if (flag_always_prepend_sign || flag_always_prepend_space_plussign || is_negative) {
                        ++additionals_len;
                    }
                    
                    if (flag_force_0x_or_dec && (specifier == 'x' || specifier == 'X')) {
                        additionals_len += 2;
                    }
                    
                    if (flag_force_0x_or_dec && specifier == 'o') {
                        ++additionals_len;
                    }
                    
                    if (width < len + additionals_len) {
                        width = len + additionals_len;
                    }
                    
                    if (!flag_left_justify && !flag_left_pad_zeroes) {
                    
                        for (i = 0; i < width - len - additionals_len; i++) {
                        
                            if ((ret = callback (user_data, ' ') <= 0)) {
                                return ret;
                            }
                            
                            ++written;
                        
                        }
                    
                    }
                    
                    if (is_negative) {
                    
                        if ((ret = callback (user_data, '-') <= 0)) {
                            return ret;
                        }
                        
                        ++written;
                    
                    } else if (flag_always_prepend_sign) {
                    
                        if ((ret = callback (user_data, '+')) <= 0) {
                            return ret;
                        }
                        
                        ++written;
                    
                    } else if (flag_always_prepend_space_plussign) {
                    
                        if ((ret = callback (user_data, ' ')) <= 0) {
                            return ret;
                        }
                        ++written;
                    
                    }
                    
                    if (!flag_left_justify && flag_left_pad_zeroes) {
                    
                        for (i = 0; i < width - len - additionals_len; i++) {
                        
                            if ((ret = callback (user_data, '0')) <= 0) {
                                return ret;
                            }
                            
                            ++written;
                        
                        }
                    
                    }
                    
                    if (flag_force_0x_or_dec && (specifier == 'x' || specifier == 'X')) {
                    
                        if ((ret = callback (user_data, '0')) <= 0) {
                            return ret;
                        }
                        
                        ++written;
                        
                        if ((ret = callback (user_data, 'x')) <= 0) {
                            return ret;
                        }
                        
                        ++written;
                    
                    } else if (flag_force_0x_or_dec && specifier == 'o') {
                    
                        if ((ret = callback (user_data, '0')) <= 0) {
                            return ret;
                        }
                        
                        ++written;
                    
                    }
                    
                    for (i = 0; i < len; i++) {
                    
                        if ((ret = callback (user_data, number_buffer[i])) <= 0) {
                            return ret;
                        }
                    
                    }
                    
                    written += len;
                    
                    if (flag_left_justify) {
                    
                        for (i = 0; i < width - len - additionals_len; i++) {
                        
                            if ((ret = callback (user_data, ' ')) <= 0) {
                                return ret;
                            }
                            
                            ++written;
                        
                        }
                    
                    }
                    
                    break;
                
                case 'f':       case 'F':
                case 'e':       case 'E':
                case 'g':       case 'G':
                case 'a':       case 'A':
                
                    switch (length) {
                    
                        case LENGTH_DEFAULT:
                        
                            dvalue = va_arg (ap, double);
                            break;
                        
                        case LENGTH_L:
                        
                            dvalue = va_arg (ap, long double);
                            break;
                        
                        default:
                        
                            return -1;
                    
                    }
                    
                    is_negative = dvalue < 0;
                    
                    if (is_negative) {
                        dvalue = -dvalue;
                    }
                    
                    value_integer = (uint64_t) dvalue;
                    value_fractional_d = dvalue - ((int64_t) dvalue);
                    
                    while (value_fractional_d - ((int64_t) value_fractional_d) != 0) {
                        value_fractional_d *= 10;
                    }
                    
                    value_fractional = (uint64_t) value_fractional_d;
                    
                    len_int = integer_to_string (number_buffer, value_integer, 10, "0123456789");
                    len_fract = integer_to_string (precision_buffer, value_fractional, 10, "0123456789");
                    
                    if (len_fract > precision) {
                        len_fract = precision;
                    }
                    
                    additionals_len = 0;
                    
                    if (flag_always_prepend_sign || flag_always_prepend_space_plussign || is_negative) {
                        ++additionals_len;
                    }
                    
                    if (flag_force_0x_or_dec) {
                        ++additionals_len;
                    } else {
                    
                        for (i = 0; i < len_fract; i++) {
                        
                            if (precision_buffer[i] != '0') {
                            
                                ++additionals_len;
                                break;
                            
                            }
                        
                        }
                    
                    }

                    if (width < len_int + additionals_len + precision) {
                        width = len_int + additionals_len + precision;
                    }
                    
                    if (!flag_left_justify && !flag_left_pad_zeroes) {
                    
                        for (i = 0; i < width - len_int - additionals_len - precision; i++) {
                        
                            if ((ret = callback (user_data, ' ')) <= 0) {
                                return ret;
                            }
                            
                            ++written;
                        
                        }
                    
                    }
                    
                    if (is_negative) {
                    
                        if ((ret = callback (user_data, '-')) <= 0) {
                            return ret;
                        }
                        
                        ++written;
                    
                    } else if (flag_always_prepend_sign) {
                    
                        if ((ret = callback (user_data, '+')) <= 0) {
                            return ret;
                        }
                        
                        ++written;
                    
                    } else if (flag_always_prepend_space_plussign) {
                    
                        if ((ret = callback (user_data, ' ')) <= 0) {
                            return ret;
                        }
                        
                        ++written;
                    
                    }
                    
                    if (!flag_left_justify && !flag_left_pad_zeroes) {
                    
                        for (i = 0; i < width - len_int - additionals_len - precision; i++) {
                        
                            if ((ret = callback (user_data, '0')) <= 0) {
                                return ret;
                            }
                            
                            ++written;
                        
                        }
                    
                    }
                    
                    for (i = 0; i < len_int; i++) {
                    
                        if ((ret = callback (user_data, number_buffer[i])) <= 0) {
                            return ret;
                        }
                    
                    }
                    
                    written += len_int;
                    
                    if (flag_force_0x_or_dec || value_fractional != 0) {
                    
                        if ((ret = callback (user_data, '.')) <= 0) {
                            return ret;
                        }
                        
                        ++written;
                        
                        for (i = 0; i < len_fract; i++) {
                        
                            if ((ret = callback (user_data, precision_buffer[i])) <= 0) {
                                return ret;
                            }
                        
                        }
                        
                        written += len_fract;
                        
                        for (i = 0; i < (len_fract == 0 ? 1 : precision - len_fract); i++) {
                        
                            if ((ret = callback (user_data, '0')) <= 0) {
                                return ret;
                            }
                            
                            ++written;
                        
                        }
                    
                    }
                    
                    if (flag_left_justify) {
                    
                        for (i = 0; i < width - len_int - additionals_len - precision; i++) {
                        
                            if ((ret = callback (user_data, ' ')) <= 0) {
                                return ret;
                            }
                            
                            ++written;
                        
                        }
                    
                    }
                    
                    break;
                
                case 'c':
                
                    switch (length) {
                    
                        case LENGTH_DEFAULT:
                        
                            value = va_arg (ap, int);
                            break;
                        
                        case LENGTH_l:
                        
                            value = va_arg (ap, uint16_t);
                            break;
                        
                        default:
                        
                            return -1;
                    
                    }
                    
                    if (width < 1) {
                        width = 1;
                    }
                    
                    if (!flag_left_justify) {
                    
                        for (i = 0; i < width - 1; i++) {
                        
                            if ((ret = callback (user_data, ' ')) <= 0) {
                                return ret;
                            }
                            
                            ++written;
                        
                        }
                    
                    }
                    
                    if ((ret = callback (user_data, value)) <= 0) {
                        return ret;
                    }
                    
                    ++written;
                    
                    if (flag_left_justify) {
                    
                        for (i = 0; i < width - 1; i++) {
                        
                            if ((ret = callback (user_data, ' ')) <= 0) {
                                return ret;
                            }
                            
                            ++written;
                        
                        }
                    
                    }
                
                    break;
                
                case 's':
                
                    if (length == LENGTH_DEFAULT) {
                    
                        char *str = va_arg (ap, char *);
                        
                        if (!str) {
                            str = "(null)";
                        }
                        
                        len = strlen (str);
                        
                        if (explicit_precision && len > precision) {
                            len = precision;
                        }
                        
                        if (width < len) {
                            width = len;
                        }
                        
                        if (!flag_left_justify) {
                        
                            for (i = 0; i < width - len; i++) {
                            
                                if ((ret = callback (user_data, ' ')) <= 0) {
                                    return ret;
                                }
                                
                                ++written;
                            
                            }
                        
                        }
                        
                        for (i = 0; i < len; i++) {
                        
                            if ((ret = callback (user_data, str[i])) <= 0) {
                                return ret;
                            }
                        
                        }
                        
                        written += len;
                        
                        if (flag_left_justify) {
                        
                            for (i = 0; i < width - len; i++) {
                            
                                if ((ret = callback (user_data, ' ')) <= 0) {
                                    return ret;
                                }
                                
                                ++written;
                            
                            }
                        
                        }
                        
                        break;
                    
                    }
                    
                    if (length == LENGTH_l) {
                    
                        uint16_t *wstr = va_arg (ap, uint16_t *);
                        
                        if (!wstr) {
                            wstr = (uint16_t *) L"(null)";
                        }
                        
                        len = 0;
                        
                        while (wstr[len]) {
                            len++;
                        }
                        
                        if (explicit_precision && len > precision) {
                            len = precision;
                        }
                        
                        if (width < len) {
                            width = len;
                        }
                        
                        if (!flag_left_justify) {
                        
                            for (i = 0; i < width - len; i++) {
                            
                                if ((ret = callback (user_data, ' ')) <= 0) {
                                    return ret;
                                }
                                
                                ++written;
                            
                            }
                        
                        }
                        
                        for (i = 0; i < len; i++) {
                        
                            if ((ret = callback (user_data, (char) wstr[i])) <= 0) {
                                return ret;
                            }
                        
                        }
                        
                        written += len;
                        
                        if (flag_left_justify) {
                        
                            for (i = 0; i < width - len; i++) {
                            
                                if ((ret = callback (user_data, ' ')) <= 0) {
                                    return ret;
                                }
                                
                                ++written;
                            
                            }
                        
                        }
                        
                        break;
                    
                    }
                    
                    return -1;
                
                default:
                
                    return -1;
            
            }
        
        } else {
        
            if ((ret = callback (user_data, *s)) <= 0) {
                return ret;
            }
            
            ++written;
        
        }
        
        STEP;
    
    }
    
    return written;

}

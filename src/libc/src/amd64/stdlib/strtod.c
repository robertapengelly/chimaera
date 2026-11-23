/******************************************************************************
 * @file            strtod.c
 *****************************************************************************/
#include    <errno.h>
#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>

#define     DECIMAL_POINT               310
#define     DECIMAL_SIZE                (DECIMAL_POINT + 1100)

static int strtod_double (unsigned char *digits, int first, int last) {

    int i, carry = 0, digit;
    
    for (i = last; i >= first; i--) {
    
        digit = digits[i] * 2 + carry;
        
        digits[i] = (unsigned char) (digit % 10);
        carry = digit / 10;
    
    }
    
    return carry;

}

double strtod (const char *nptr, char **endptr) {

    const char *p = nptr, *start, *end, *q;
    unsigned char digits[DECIMAL_SIZE];
    
    int negative = 0, seen = 0, dot = 0, nonzero = 0, eneg = 0;
    int point = 0, exponent = 0;
    int first, last, i, j, digit, carry, sticky = 0, binary = 0;
    
    uint32_t high = 0, low = 0;
    
    union {
    
        double value;
        uint32_t words[2];
    
    } result;
    
    if (endptr) {
        *endptr = (char *) nptr;
    }
    
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) {
        p++;
    }
    
    if (*p == '-' || *p == '+') {
    
        negative = (*p == '-');
        p++;
    
    }
    
    start = p;
    
    for (; *p; p++) {
    
        if (*p == '.' && !dot) {
        
            dot = 1;
            continue;
        
        }
        
        if (*p < '0' || *p > '9') {
            break;
        }
        
        seen = 1;
        
        if (*p != '0') {
            nonzero = 1;
        }
        
        if (!dot && nonzero && point < 1000000000) {
            point++;
        } else if (dot && !nonzero && point + 1000000000 > 0) {
            point--;
        }
    
    }
    
    if (!seen) {
        return 0.0;
    }
    
    end = p;
    
    if (*p == 'e' || *p == 'E') {
    
        q = p + 1;
        
        if (*q == '-' || *q == '+') {
        
            eneg = (*q == '-');
            q++;
        
        }
        
        if (*q >= '0' && *q <= '9') {
        
            for (; *q >= '0' && *q <= '9'; q++) {
            
                if (exponent < 100000000) {
                    exponent = exponent * 10 + (*q - '0');
                } else {
                    exponent = 1000000000;
                }
            
            }
            
            if (eneg) {
                point -= exponent;
            } else {
                point += exponent;
            }
            
            p = q;
        
        }
    
    }
    
    if (endptr) {
        *endptr = (char *) p;
    }
    
    if (!nonzero) {
        goto done;
    }
    
    if (point > 309) {
    
        errno = ERANGE;
        goto overflow;
    
    }
    
    if (point + 323 < 0) {
    
        errno = ERANGE;
        goto done;
    
    }
    
    memset (digits, 0, sizeof (digits));
    
    first = DECIMAL_POINT - point;
    
    last = first;
    i = first;
    
    nonzero = 0;
    
    for (p = start; p < end; p++) {
    
        if (*p == '.' || (!nonzero && *p == '0')) {
            continue;
        }
        
        nonzero = 1;
        
        if (i < DECIMAL_SIZE) {
        
            digits[i] = (unsigned char) (*p - '0');
            
            if (digits[i]) {
                last = i;
            }
            
            i++;
        
        } else if (*p != '0') {
            sticky = 1;
        }
    
    }
    
    while (first < DECIMAL_POINT - 1 || (first == DECIMAL_POINT - 1 && digits[first] >= 2)) {
    
        carry = 0;
        
        for (i = first; i <= last; i++) {
        
            digit = carry * 10 + digits[i];
            
            digits[i] = (unsigned char) (digit / 2);
            carry = digit % 2;
        
        }
        
        if (carry) {
        
            if (last < DECIMAL_SIZE - 1) {
                digits[++last] = 5;
            } else {
                sticky = 1;
            }
        
        }
        
        if (!digits[first]) {
            first++;
        }
        
        binary++;
        
        if (binary > 1023) {
        
            errno = ERANGE;
            goto overflow;
        
        }
    
    }
    
    while (first >= DECIMAL_POINT && binary + 1022 > 0) {
    
        carry = strtod_double (digits, first, last);
        
        if (carry) {
            digits[--first] = (unsigned char) carry;
        }
        
        binary--;
    
    }
    
    low = digits[DECIMAL_POINT - 1];
    digits[DECIMAL_POINT - 1] = 0;
    
    first = DECIMAL_POINT;
    
    while (first <= last && !digits[first]) {
        first++;
    }
    
    for (j = 0; j < 52; j++) {
    
        carry = strtod_double (digits, first, last);
        
        if (first > DECIMAL_POINT && carry) {
        
            digits[--first] = (unsigned char) carry;
            carry = 0;
        
        }
        
        high = (high << 1) | (low >> 31);
        low = (low << 1) | (uint32_t) carry;
        
        while (first <= last && !digits[first]) {
            first++;
        }
    
    }
    
    carry = strtod_double (digits, first, last);
    
    if (first == DECIMAL_POINT && carry) {
    
        for (i = first; i <= last; i++) {
        
            if (digits[i]) {
                sticky = 1;
                break;
            }
        
        }
        
        if (sticky || (low & 1U)) {
        
            low++;
            
            if (!low) {
                high++;
            }
        
        }
    
    }
    
    if (high & 0x00200000U) {
    
        high >>= 1;
        binary++;
    
    }
    
    if (binary > 1023) {
    
        errno = ERANGE;
        goto overflow;
    
    }
    
    if (high & 0x00100000U) {
        high = (high & 0x000FFFFFU) | ((uint32_t) (binary + 1023) << 20);
    } else {
        errno = ERANGE;
    }
    
    goto done;

overflow:

    high = 0x7FF00000U;
    low = 0;

done:

    result.words[0] = low;
    result.words[1] = high | (negative ? 0x80000000U : 0U);
    
    return result.value;

}

/******************************************************************************
 * @file            ctype.h
 *****************************************************************************/
#ifndef     _CTYPE_H
#define     _CTYPE_H

__inline__ int isalpha (int ch) {
    return ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'));
}

__inline__ int isalnum (int ch) {
    return ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'));
}

__inline__ int isdigit (int ch) {
    return (ch >= '0' && ch <= '9');
}

__inline__ int islower (int ch) {
    return (ch >= 'a' && ch <= 'z');
}

__inline__ int isupper (int ch) {
    return (ch >= 'A' && ch <= 'Z');
}

__inline__ int isxdigit (int ch) {
    return ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'));
}

__inline__ int toupper (int ch) {

    if (ch >= 'a' && ch <= 'z') {
        return ch & ~0x20;
    }
    
    return ch;

}

__inline__ int tolower (int ch) {

    if (ch >= 'A' && ch <= 'Z') {
        return ch | 0x20;
    }
    
    return ch;

}

#endif      /* _CTYPE_H */

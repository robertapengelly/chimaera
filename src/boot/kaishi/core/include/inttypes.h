/******************************************************************************
 * @file            inttypes.h
 *****************************************************************************/
#ifndef     _INTTYPES_H
#define     _INTTYPES_H

#define     PRIx64                      "llx"
#define     PRIX64                      "llX"

#if     defined (__AMD64__)
# define        PRIxPTR                 "llx"
# define        PRIXPTR                 "llX"
#else
# define        PRIxPTR                 "lx"
# define        PRIXPTR                 "lX"
#endif

#endif      /* _INTTYPES_H */

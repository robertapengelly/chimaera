/******************************************************************************
 * @file            stddef.h
 *****************************************************************************/
#ifndef     _STDDEF_H

#if     !defined (__NEED_NULL) && !defined (__NEED_WCHAT_T) && !defined (__NEED_SIZE_T) && !defined (__NEED_PTRDIFF_T)
# define        _STDDEF_H
# define        _STDDEF_H_
#endif

#if     defined (_STDDEF_H) || defined (__NEED_NULL)
# ifdef     NULL
#  undef        NULL
# endif

# define        NULL                    ((void *) 0)

# if        defined (__NEED_NULL)
#  undef        __NEED_NULL
# endif
#endif

#if     defined (_STDDEF_H) || defined (__NEED_WCHAR_T)
typedef     unsigned short              wchar_t;
# if        defined (__NEED_WCHAR_T)
#  undef        __NEED_WCHAR_T
# endif
#endif


#if     defined (_STDDEF_H) || defined (__NEED_SIZE_T)
# if        defined (__AMD64__)
typedef     unsigned long long          size_t;
# else
typedef     unsigned int                size_t;
# endif
# if        defined (__NEED_SIZE_T)
#  undef        __NEED_SIZE_T
# endif
#endif

#if     defined (_STDDEF_H) || defined (__NEED_PTRDIFF_T)
# if        defined (__AMD64__)
typedef     signed long long            ptrdiff_t;
# else
typedef     signed int                  ptrdiff_t;
# endif
# if        defined (__NEED_PTRDIFF_T)
#  undef        __NEED_PTRDIFF_T
# endif
#endif

#if     defined (_STDDEF_H)
# define        offsetof(type, member)                      ((size_t) (&((type *) 0)->member))
#endif

#endif      /* _STDDEF_H */

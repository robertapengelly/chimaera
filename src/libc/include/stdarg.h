/******************************************************************************
 * @file            stdarg.h
 *****************************************************************************/
#ifndef     _STDARG_H
#define     _STDARG_H

typedef     char *                      va_list;

#if     defined (__AMD64__)
# define    va_start(ap, parmN)         ap = (char *) &parmN + ((sizeof (parmN) < 8) ? 8 : sizeof (parmN))
# define    va_end(ap)                  ap = (char *) 0

# if    defined (__SCC__)
#  define       va_arg(ap, type)            __scc_builtin_va_arg (ap, type)
# else
#  define       va_arg(ap, type)            *(type *) (ap += (sizeof(type) < 8) ? 8 : sizeof (type), ap - ((sizeof (type) < 8) ? 8 : sizeof (type)))
# endif
#else
# define    va_start(ap, parmN)         ap = (char *) &parmN + sizeof (parmN)
# define    va_end(ap)                  ap = (char *) 0

# if    defined (__SCC__)
#  define       va_arg(ap, type)            __scc_builtin_va_arg (ap, type)
# else
#  define       va_arg(ap, type)            *(type *) (ap += sizeof (type), ap - sizeof (type))
# endif
#endif

#endif      /* _STDARG_H */

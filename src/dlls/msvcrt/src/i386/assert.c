/******************************************************************************
 * @file            assert.c
 *****************************************************************************/
#include    <stdio.h>
#include    <stdlib.h>

__attribute__ ((dllexport)) void __assert_func (const char *file, int line, const char *func, const char *failedexpr) {

    fprintf (stderr, "assertion failed: %s, file %s, line %d, function %s\n", failedexpr, file, line, func);
    abort ();

}

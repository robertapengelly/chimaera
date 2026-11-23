/******************************************************************************
 * @file            assert.c
 *****************************************************************************/
#include    <stdio.h>
#include    <stdlib.h>

void __assert_func (const char *file, int line, const char *func, const char *failedexpr) {

    fprintf (stderr, "sssertion failed: %s, file %s, line %d, function %s\n", failedexpr, file, line, func);
    abort ();

}

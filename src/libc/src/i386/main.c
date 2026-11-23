/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <limits.h>
#include    <stdint.h>

extern char **__environ;

extern int main (int argc, char **argv);
extern void __init_stdio (void);

static int __parse_args (uintptr_t *stack, int *out_argc, char ***out_args) {

    if (!stack || *stack > INT_MAX) {
        return -1;
    }
    
    *out_argc = (int) *stack;
    *out_args = (char **) (stack + 1);
    
    return 0;

}

int __real_start (uintptr_t *stack) {

    char **argv = 0;
    int argc = 0;
    
    __init_stdio ();
    
    if (__parse_args (stack, &argc, &argv) == 0) {
    
        __environ = (char **) (argv + argc + 1);
        return main (argc, argv);
    
    }
    
    return 1;

}

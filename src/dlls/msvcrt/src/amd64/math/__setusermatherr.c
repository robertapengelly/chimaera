/******************************************************************************
 * @file            __setusermatherr.c
 *****************************************************************************/
struct _exception {

    int type;
    char *name;
    
    double arg1;
    double arg2;
    double retval;

};

typedef int (*matherr_handler_t) (struct _exception *);
static matherr_handler_t user_matherr = 0;

__attribute__ ((dllexport)) void __setusermatherr (matherr_handler_t handler) {
    user_matherr = handler;
}

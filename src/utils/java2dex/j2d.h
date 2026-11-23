/******************************************************************************
 * @file            j2d.h
 *****************************************************************************/
#ifndef     _J2D_H
#define     _J2D_H

#include    <stdio.h>

struct j2d_state {
    
    const char *ofile;
    FILE *ofp, *ifp;
    
    char **input_files;
    long nb_input_files;
    
    unsigned long max_errors;

};

extern struct j2d_state *state;
extern const char *program_name;

#endif      /* _J2D_H */

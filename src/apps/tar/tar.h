/******************************************************************************
 * @file            tar.h
 *****************************************************************************/
#ifndef     _TAR_H
#define     _TAR_H

struct tar_state {

    char **paths;
    long nb_paths;
    
    char *target, *root, *mode;
    int append, create, extract;

};

extern struct tar_state *state;
extern const char *program_name;

#endif      /* _TAR_H */

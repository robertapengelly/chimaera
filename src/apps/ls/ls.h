/******************************************************************************
 * @file            ls.h
 *****************************************************************************/
#ifndef     _LS_H
#define     _LS_H

struct ls_state {

    char **paths;
    long nb_paths;
    
    int long_listing;
    int list_hidden;

};

extern struct ls_state *state;
extern const char *program_name;

#endif      /* _LS_H */

/******************************************************************************
 * @file            j2d.c
 *****************************************************************************/
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "int64.h"
#include    "j2d.h"
#include    "lex.h"
#include    "lib.h"
#include    "report.h"
#include    "token.h"

struct j2d_state *state = 0;
const char *program_name = 0;

static void cleanup (void) {

    if (state->ofp && state->ofp != stdout) { fclose (state->ofp); }
    if (state->ifp && state->ifp != stdin) { fclose (state->ifp); }
    
    if (get_error_count () > 0) {
    
        if (state->ofile) {
            remove (state->ofile);
        }
    
    }

}

extern int compile_main (int argc, char **argv);

int main (int argc, char **argv) {

    const char *p;
    long i;
    
    char **cargv;
    int cargc, ret;
    
    if (argc && *argv) {
    
        program_name = *argv;
        
        if ((p = strrchr (program_name, '/')) || (p = strrchr (program_name, '\\'))) {
            program_name = (p + 1);
        }
    
    }
    
    atexit (cleanup);
    lex_init ();
    
    state = xmalloc (sizeof (*state));
    parse_args (argc, argv, 1);
    
    if (!state->nb_input_files) {
    
        report_at (program_name, 0, REPORT_ERROR, "no input file specified");
        return EXIT_FAILURE;
    
    }
    
    if (!state->ofile) {
    
        if (state->nb_input_files == 1) {
        
            if (strcmp (state->input_files[0], "-") == 0) {
            
                report_at (program_name, 0, REPORT_ERROR, "-o needed when input is from standard input");
                return EXIT_FAILURE;
            
            }
        
        }
    
    }
    
    if (state->nb_input_files == 1) {
    
        char *args[4];
        
        if (!state->ofile) {
        
            const char * ifile = state->input_files[0];
            char *temp;
            
            if ((p = strrchr (ifile, '.'))) {
            
                temp = xmalloc (p - ifile + 5);
                sprintf (temp, "%.*s.dex", (int) (p - ifile), ifile);
            
            } else {
            
                temp = xmalloc (strlen (ifile) + 5);
                sprintf (temp, "%s.dex", ifile);
            
            }
            
            state->ofile = temp;
        
        }
        
        if (!(state->ofp = fopen (state->ofile, "wb"))) {
            return EXIT_FAILURE;
        }
        
        args[0] = (char *) program_name;
        args[1] = state->input_files[0];
        args[2] = (char *) state->ofile;
        args[3] = 0;
        
        ret = compile_main (3, args);
    
    } else {
    
        if (state->ofile) {
        
            if (!(state->ofp = fopen (state->ofile, "wb"))) {
                return EXIT_FAILURE;
            }
            
            cargc = (int) state->nb_input_files + 3;
            cargv = xmalloc (sizeof (*cargv) * (cargc + 1));
            
            cargv[0] = (char *) program_name;
            cargv[1] = "--single-out";
            cargv[2] = (char *) state->ofile;
            
            for (i = 0; i < state->nb_input_files; i++) {
                cargv[i + 3] = state->input_files[i];
            }
            
            cargv[cargc] = 0;
            ret = compile_main (cargc, cargv);
        
        } else {
        
            const char *ifile;
            char *temp;
            
            char *args[4];
            long idx;
            
            for (idx = 0; idx < state->nb_input_files; idx++) {
            
                ifile = state->input_files[idx];
                
                if ((p = strrchr (ifile, '.'))) {
                
                    temp = xmalloc (p - ifile + 5);
                    sprintf (temp, "%.*s.dex", (int) (p - ifile), ifile);
                
                } else {
                
                    temp = xmalloc (strlen (ifile) + 5);
                    sprintf (temp, "%s.dex", ifile);
                
                }
                
                state->ofile = temp;
                
                if (!(state->ofp = fopen (state->ofile, "wb"))) {
                    return EXIT_FAILURE;
                }
                
                args[0] = (char *) program_name;
                args[1] = state->input_files[idx];
                args[2] = (char *) state->ofile;
                args[3] = 0;
                
                if ((ret = compile_main (3, args))) {
                    return EXIT_FAILURE;
                }
                
                fclose (state->ofp);
                state->ofp = 0;
            
            }
        
        }
    
    }
    
    return ret ? EXIT_FAILURE : (get_error_count () > 0 ? EXIT_FAILURE : EXIT_SUCCESS);

}

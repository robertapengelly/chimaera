/******************************************************************************
 * @file            pkg.c
 *****************************************************************************/
#include    <stdlib.h>
#include    <string.h>

#include    "pkg.h"
#include    "lib.h"

#include    "report.h"
#include    "manifest.h"

extern int pkg_write_package (struct pkg_manifest *m);

struct pkg_state *state = 0;
const char *program_name = 0;

static void cleanup (void) {

    if (state->ofp) { fclose (state->ofp); }
    
    if (get_error_count () > 0) {
    
        if (state->ofile) {
            remove (state->ofile);
        }
    
    }

}

int main (int argc, char **argv) {

    const char *p;
    
    if (argc && *argv) {
    
        program_name = *argv;
        
        if ((p = strrchr (program_name, '/')) || (p = strrchr (program_name, '\\'))) {
            program_name = (p + 1);
        }
    
    }
    
    atexit (cleanup);
    
    state = xmalloc (sizeof (*state));
    parse_args (argc, argv, 1);
    
    if (state->nb_args != 2) {
    
        report_at (program_name, 0, REPORT_ERROR, "Usage: %s manifest.txt output.pkg", program_name);
        return EXIT_FAILURE;
    
    }
    
    state->ofile = state->args[1];
    init_parser ();
    
    if (parse_input_file (state->args[0])) {
        return EXIT_FAILURE;
    }
    
    if (state->manifest.file_count == 0) {
    
        report_at (program_name, 0, REPORT_ERROR, "no files listed");
        return EXIT_FAILURE;
    
    }
    
    if (pkg_write_package (&state->manifest)) {
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;

}

/******************************************************************************
 * @file            regf.c
 *****************************************************************************/
#include    <stdlib.h>
#include    <string.h>

#include    "regf.h"
#include    "lib.h"
#include    "report.h"
#include    "tree.h"
#include    "writer.h"

struct regf_state *state = 0;
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

    const char *root_name = "", *p;
    
    stats_t s;
    rf_key_t *root;
    
    if (argc && *argv) {
    
        program_name = *argv;
        
        if ((p = strrchr (program_name, '/')) || (p = strrchr (program_name, '\\'))) {
            program_name = (p + 1);
        }
    
    }
    
    atexit (cleanup);
    
    state = xmalloc (sizeof (*state));
    parse_args (argc, argv, 1);
    
    if (state->mode == MODE_SET) {
    
        if (!create_or_set ()) {
            return EXIT_FAILURE;
        }
        
        return EXIT_SUCCESS;
    
    }
    
    root = key_new (root_name, 0);
    init_parser ();
    
    if (!parse_input_file (state->args[0], root)) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to parse '%s'", state->args[0]);
        return EXIT_FAILURE;
    
    }
    
    if (get_error_count () > 0) {
        return EXIT_FAILURE;
    }
    
    validate_tree_rec (root);
    
    if (get_error_count () > 0) {
        return EXIT_FAILURE;
    }
    
    root = collapse_synthetic_root (root);
    
    memset (&s, 0, sizeof (s));
    stats_collect (root, 0, &s);
    
    if (state->debug) {
    
        printf ("tree summary:\n");
        printf ("\n");
        printf ("    keys      : %u\n", s.key_count);
        printf ("    values    : %u\n", s.value_count);
        printf ("    max depth : %u\n", s.max_depth);
    
    }
    
    if (!write_hive_file (state->args[1], root)) {
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;

}

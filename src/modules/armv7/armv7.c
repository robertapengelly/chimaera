/******************************************************************************
 * @file            armv7.c
 *****************************************************************************/
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "armv7.h"
#include    "report.h"

const char *program_name = 0;

int armv7_main (int argc, char **argv) {

    Mach cpu;
    char *p;
    
    if (argc && *argv) {
    
        program_name = *argv;
        
        if ((p = strrchr (program_name, '/')) || (p = strrchr (program_name, '\\'))) {
            program_name = (p + 1);
        }
    
    }
    
    memset (&cpu, 0, sizeof (cpu));
    
    if (argc < 1 || !argv[0]) {
    
        report_at (program_name, 0, REPORT_ERROR, "no executable specified");
        return EXIT_FAILURE;
    
    }
    
    cpu.mem_size = MEM_SIZE;
    
    if (!(cpu.mem = malloc (cpu.mem_size))) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to allocate memory");
        return EXIT_FAILURE;
    
    }
    
    memset (cpu.mem, 0, cpu.mem_size);
    cpu.r[R_SP] = cpu.mem_size - 4;
    
    load_file (&cpu, argv[0], argc, argv, argv + argc + 1);
    cpu.running = 1;
    
    while (cpu.running) {
        step (&cpu);
    }
    
    return EXIT_SUCCESS;

}

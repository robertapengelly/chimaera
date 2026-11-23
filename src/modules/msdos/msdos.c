/******************************************************************************
 * @file            msdos.c
 *****************************************************************************/
#include    <sys/ioctl.h>

#include    <ctype.h>
#include    <errno.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <termios.h>
#include    <time.h>
#include    <unistd.h>

#include    "cpu.h"
#include    "loader.h"
#include    "interrupts.h"
#include    "heap.h"
#include    "memory.h"
#include    "psp.h"

#define     CONVENTIONAL_MEM_SIZE       (640UL << 10)
#define     EXE_LOAD_SEG                0x00E0
#define     MEM_SIZE                    (1UL << 20)

static uint64_t cpu_clock_ns (void) {

    struct timespec now;
    
    if (clock_gettime (CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    
    return (uint64_t) now.tv_sec * 1000000000ULL + now.tv_nsec;

}

static void cpu_sleep_ns (uint64_t duration) {

    struct timespec delay;
    
    delay.tv_sec = (time_t) (duration / 1000000000ULL);
    delay.tv_nsec = (long) (duration % 1000000000ULL);
    
    while (nanosleep (&delay, &delay) != 0 && errno == EINTR) {}

}

/* Approximate 8086 bus/execution timing, not a cycle-exact model. */
static unsigned cpu_8086_cycles (CPU8086 *cpu, uint8_t op, uint8_t modrm, uint32_t next_ip, uint16_t old_cx, int repeated) {

    unsigned cycles = 4;
    unsigned memory_operand = (modrm & 0xC0) != 0xC0;
    
    if (op >= 0x70 && op <= 0x7F) {
        return cpu->ip != next_ip ? 16 : 4;
    }
    
    if (op >= 0xB0 && op <= 0xBF) {
        return 4;
    }
    
    if (op >= 0x40 && op <= 0x5F) {
        return op >= 0x50 ? 11 : 3;
    }
    
    if (op <= 0x3B && (op & 7) <= 3) {
        return memory_operand ? 16 : 3;
    }
    
    switch (op) {
    
        case 0x88:  case 0x89:  case 0x8A:  case 0x8B:
        
            cycles = memory_operand ? 13 : 2;
            break;
        
        case 0x80:  case 0x81:  case 0x83:
        
            cycles = memory_operand ? 17 : 4;
            break;
        
        case 0x90:
        
            cycles = 3;
            break;
        
        case 0xA4:  case 0xA5:
        
            cycles = 18;
            break;
        
        case 0xA6:  case 0xA7:
        
            cycles = 22;
            break;
        
        case 0xAA:  case 0xAB:
        
            cycles = 11;
            break;
        
        case 0xAC:  case 0xAD:
        
            cycles = 12;
            break;
        
        case 0xAE:  case 0xAF:
        
            cycles = 15;
            break;
        
        case 0xE0:  case 0xE1:  case 0xE2:
        
            cycles = cpu->ip != next_ip ? 17 : 5;
            break;
        
        case 0xE3:
        
            cycles = cpu->ip != next_ip ? 18 : 6;
            break;
        
        case 0xE8:  case 0x9A:
        
            cycles = 23;
            break;
        
        case 0xC2:  case 0xC3:  case 0xCA:  case 0xCB:
        
            cycles = 20;
            break;
        
        case 0xCD:  case 0xCC:
        
            cycles = 51;
            break;
        
        case 0xE9:  case 0xEB:
        
            cycles = 15;
            break;
        
        case 0xF6:  case 0xF7:
        
            cycles = memory_operand ? 24 : 12;
            break;
        
        default:
        
            cycles = 4;
            break;
    
    }
    
    if (repeated && op >= 0xA4 && op <= 0xAF) {
        return 2 + cycles * (unsigned) (uint16_t) (old_cx - cpu->regs[REG_CX]);
    }
    
    return cycles;

}

static struct termios terminal_original_input;
static int terminal_input_mode_changed;

static void initialize_terminal_input (void) {

    struct termios mode;
    
    if (!isatty (STDIN_FILENO) || tcgetattr (STDIN_FILENO, &mode) != 0) {
        return;
    }
    
    terminal_original_input = mode;
    
    mode.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    mode.c_iflag &= ~(IXON | ICRNL);
    mode.c_cc[VMIN] = 1;
    mode.c_cc[VTIME] = 0;
    
    if (tcsetattr (STDIN_FILENO, TCSANOW, &mode) == 0) {
        terminal_input_mode_changed = 1;
    }

}

static void restore_terminal_input (void) {

    if (terminal_input_mode_changed) {
        tcsetattr (STDIN_FILENO, TCSANOW, &terminal_original_input);
    }

}

static unsigned terminal_original_columns, terminal_original_rows;
static unsigned terminal_resized_columns, terminal_resized_rows;

static int terminal_size_changed;

static void resize_terminal_output (void) {

    struct winsize window;
    unsigned columns, rows;
    
    if (!isatty (STDOUT_FILENO) || ioctl (STDOUT_FILENO, TIOCGWINSZ, &window) != 0 || !window.ws_col || !window.ws_row) {
        return;
    }
    
    columns = window.ws_col < 80 ? 80 : window.ws_col;
    rows = window.ws_row < 25 ? 25 : window.ws_row;
    
    if (columns != window.ws_col || rows != window.ws_row) {
    
        terminal_original_columns = window.ws_col;
        terminal_original_rows = window.ws_row;
        
        terminal_resized_columns = columns;
        terminal_resized_rows = rows;
        
        terminal_size_changed = 1;
        fprintf (stdout, "\033[8;%u;%ut", rows, columns);
        
        fflush (stdout);
    
    }

}

static void restore_terminal_output_size (void) {

    if (!terminal_size_changed) {
        return;
    }
    
    struct winsize window;
    
    if (!isatty (STDOUT_FILENO) || ioctl (STDOUT_FILENO, TIOCGWINSZ, &window) != 0) {
        return;
    }
    
    if (window.ws_col == terminal_resized_columns && window.ws_row == terminal_resized_rows) {
    
        fprintf (stdout, "\033[8;%u;%ut", terminal_original_rows, terminal_original_columns);
        fflush (stdout);
    
    }

}

static void restore_terminal_cursor (void) {

    {
    
        fputs ("\033[0m\033[?7h\033[?25h", stdout);
        fflush (stdout);
    
    }
    
    restore_terminal_input ();
    restore_terminal_output_size ();

}

void init_psp (const char *path, CPU8086 *cpu, const char *args, uint16_t base_seg) {

    uint32_t env_base, p;
    uint16_t env_seg;
    
    cpu->psp_seg = base_seg - 0x10;
    
    env_seg = cpu->psp_seg - 0x10;
    env_base = (uint32_t) env_seg << 4;
    
    p = env_base;
    
    /* initial command interpreter path */
    for (const char *s = "COMSPEC="; *s; s++) {
        mem_write8 (cpu->mem, cpu->mem_size, p++, (uint8_t) *s);
    }
    
    for (const char *s = path; *s; s++) {
        mem_write8 (cpu->mem, cpu->mem_size, p++, (uint8_t) *s);
    }
    
    mem_write8 (cpu->mem, cpu->mem_size, p++, 0);
    mem_write8 (cpu->mem, cpu->mem_size, p++, 0);
    
    /* Count of extra strings following the environment = 1 */
    mem_write16 (cpu->mem, cpu->mem_size, p, 0x0001);
    p += 2;
    
    for (const char *s = path; *s; s++) {
    
        char ch = *s;
        
        if (ch == '/') {
            ch = '\\';
        }
        
        mem_write8 (cpu->mem, cpu->mem_size, p++, (uint8_t) toupper((int) ch));
    
    }
    
    mem_write8 (cpu->mem, cpu->mem_size, p++, 0);
    init_psp_contents (cpu, args, cpu->psp_seg, 0x0000, env_seg, DOS_MEMORY_END_SEG);
    
    /* Environment MCB */
    {
    
        uint32_t env_mcb_phys = (uint32_t) (env_seg - 1) << 4;
        
        /* paragraphs owned by environment block, excluding the MCB itself */
        uint16_t env_block_size = 0x000F;                   /* 15 paragraphs => 0x00C0..0x00CE */
        
        mem_write8 (cpu->mem, cpu->mem_size, env_mcb_phys + 0x00, 'M');
        
        mem_write16 (cpu->mem, cpu->mem_size, env_mcb_phys + 0x01, cpu->psp_seg);              /* owner = PSP */
        mem_write16 (cpu->mem, cpu->mem_size, env_mcb_phys + 0x03, env_block_size);
        
        for (int i = 5; i < 16; i++) {
            mem_write8 (cpu->mem, cpu->mem_size, env_mcb_phys + i, 0);
        }
    
    }
    
    /* PSP MCB */
    {
    
        uint32_t mcb_phys = (uint32_t) (cpu->psp_seg - 1) << 4;
        uint16_t block_size = (uint16_t) (DOS_MEMORY_END_SEG - cpu->psp_seg);
        
        mem_write8 (cpu->mem, cpu->mem_size, mcb_phys + 0x00, 'Z');
        
        mem_write16 (cpu->mem, cpu->mem_size, mcb_phys + 0x01, cpu->psp_seg);
        mem_write16 (cpu->mem, cpu->mem_size, mcb_phys + 0x03, block_size);
        
        for (int i = 5; i < 16; i++) {
            mem_write8 (cpu->mem, cpu->mem_size, mcb_phys + i, 0);
        }
    
    }
    
    cpu->ds = cpu->es = cpu->psp_seg;

}

int msdos_main (int argc, char **argv) {

    uint8_t *mem;
    CPU8086 cpu;
    
    const char *path = argc && argv ? *argv : NULL;
    const char *dos_path;
    
    char cmd_tail[128] = {0};
    int rc;
    
    int offset = 0, len, i;
    unsigned long steps = 0;
    
    uint8_t timing_op = 0, timing_modrm = 0;
    size_t loaded = 0;
    
    uint32_t timing_next_ip = 0;
    uint16_t timing_old_cx;
    
    int timing_rep = 0;
    
    double cpu_hz = 0;
    uint64_t cpu_cycles = 0;
    
    unsigned cpu_batch = 0;
    uint64_t cpu_deadline = 0;
    
    const char *cpu_mhz_env;
    const char *cpu_name;
    
    cpu_name = getenv ("MSDOS_CPU");
    cpu_mhz_env = getenv ("MSDOS_MHZ");
    
    if (cpu_name != NULL && strcmp (cpu_name, "8086") != 0) {
    
        fprintf (stderr, "Unsupported MSDOS_CPU: %s\n", cpu_name);
        return 1;
    
    }
    
    if (cpu_mhz_env != NULL && *cpu_mhz_env != '\0') {
    
        char *end;
        double mhz;
        
        errno = 0;
        mhz = strtod (cpu_mhz_env, &end);
        
        if (errno != 0 || *end != '\0' || !(mhz > 0.0 && mhz <= 1000.0)) {
        
            fprintf (stderr, "Invalid MSDOS_MHZ value: %s\n", cpu_mhz_env);
            return 1;
        
        }
        
        cpu_hz = mhz * 1000000.0;
    
    } else if (cpu_name != NULL) {
        cpu_hz = 4770000.0;
    }
    
    for (i = 1; i < argc; i++) {
    
        len = strlen (argv[i]);
        
        if (offset + len + 2 < 126) {
        
            strcat(cmd_tail, " ");
            strcat(cmd_tail, argv[i]);
            
            offset += len + 1;
        
        }
    
    }

    if (path == NULL) {
        return 1;
    }
    
    mem = (uint8_t *) calloc ((MEM_SIZE), 1);
    
    if (mem == NULL) {
    
        fprintf (stderr, "Out of memory\n");
        return 1;
    
    }
    
    cpu_reset (&cpu, mem, MEM_SIZE);
    dos_path = dos_register_initial_program (path);
    
    if (load_mz_file (path, &cpu, EXE_LOAD_SEG, (uint16_t) ((CONVENTIONAL_MEM_SIZE - ((uint32_t) EXE_LOAD_SEG << 4)) / 16))) {
        init_psp (dos_path, &cpu, cmd_tail, EXE_LOAD_SEG);
    } else if (load_com_file (path, &cpu, mem, CONVENTIONAL_MEM_SIZE, EXE_LOAD_SEG, &loaded)) {
        init_psp (dos_path, &cpu, cmd_tail, EXE_LOAD_SEG);
    } else {
    
        fprintf (stderr, "Failed to load %s\n", path);
        
        free (mem);
        return 1;
    
    }
    
    /* DOS programs begin with maskable interrupts enabled. */
    cpu.flags |= 0x0200;
    
    if (atexit (restore_terminal_cursor) != 0) {
    
        fprintf (stderr, "Failed to register terminal cursor cleanup\n");
        
        free (mem);
        return 1;
    
    }
    
    resize_terminal_output ();
    initialize_terminal_input ();
    
    /* init_dos_heap(&cpu, 0x2000); */
    init_ivt (&cpu);
    
    if (cpu_hz > 0) {
        cpu_deadline = cpu_clock_ns ();
    }
    
    while (!cpu.halted) {
    
        if ((steps & 0x0FFF) == 0) {
            bios_timer_refresh (&cpu);
        }
        
        if ((steps & 0x3FFF) == 0) {
            bios_keyboard_poll (&cpu);
        }
        
        timing_op = 0, timing_modrm = 0;
        timing_next_ip = 0;
        
        timing_old_cx = cpu.regs[REG_CX];
        timing_rep = 0;
        
        if (cpu_hz > 0) {
        
            uint32_t timing_ip = cpu.ip;
            uint32_t timing_address;
            
            unsigned prefix_count = 0;
            
            do {
            
                timing_address = ((uint32_t) cpu.cs << 4) + (uint16_t) timing_ip;
                timing_op = timing_address < cpu.mem_size ? cpu.mem[timing_address] : 0x90;
                
                timing_ip++;
                
                if (timing_op == 0xF2 || timing_op == 0xF3) {
                    timing_rep = 1;
                }
            
            } while (++prefix_count < 15 && (timing_op == 0xF2 || timing_op == 0xF3 || timing_op == 0x26 || timing_op == 0x2E || timing_op == 0x36 || timing_op == 0x3E || timing_op == 0xF0));
            
            timing_address = ((uint32_t) cpu.cs << 4) + (uint16_t) timing_ip;
            timing_modrm = timing_address < cpu.mem_size ? cpu.mem[timing_address] : 0xC0;
            timing_next_ip = (uint16_t) (timing_ip + 1);
            
            if ((timing_op >= 0x70 && timing_op <= 0x7F) || (timing_op >= 0xE0 && timing_op <= 0xE3)) {
                timing_next_ip = (uint16_t) (timing_ip + 1);
            }
        
        }
        
        rc = cpu_step (&cpu);
        
        if ((steps & 0xFF) == 0) {
            bios_video_refresh (&cpu);
        }
        
        if (rc <= 0) {
        
            free (mem);
            return 1;
        
        }
        
        steps++;
        
        if (cpu_hz > 0) {
        
            cpu_cycles += cpu_8086_cycles (&cpu, timing_op, timing_modrm, timing_next_ip, timing_old_cx, timing_rep);
            
            if (++cpu_batch == 256) {
            
                uint64_t duration = (uint64_t) ((double) cpu_cycles * 1000000000.0 / cpu_hz);
                uint64_t now;
                
                cpu_batch = 0;
                cpu_cycles = 0;
                
                cpu_deadline += duration;
                now = cpu_clock_ns ();
                
                if (cpu_deadline > now) {
                    cpu_sleep_ns (cpu_deadline - now);
                } else if (now - cpu_deadline > 100000000ULL) {
                    cpu_deadline = now;
                }
            
            }
        
        }
    
    }
    
    free (mem);
    return 0;

}

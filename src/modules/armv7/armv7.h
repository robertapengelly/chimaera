/******************************************************************************
 * @file            armv7.h
 *****************************************************************************/
#ifndef     _ARMV7_H
#define     _ARMV7_H

#define     LOAD_ADDR                   0x00010000U

#include    <stddef.h>
#include    <stdint.h>

//#define     MEM_SIZE                    (4UL << 20)
#define     MEM_SIZE                    (2UL << 20)

#define     R_SP                        13
#define     R_LR                        14
#define     R_PC                        15

#define     N_FLAG                      0x80000000U
#define     Z_FLAG                      0x40000000U
#define     C_FLAG                      0x20000000U
#define     V_FLAG                      0x10000000U

#define     MAX_MEM_REGIONS             128

#define     MEM_PROT_READ               0x01U
#define     MEM_PROT_WRITE              0x02U
#define     MEM_PROT_EXEC               0x04U

#define     MEM_KIND_NORMAL             0
#define     MEM_KIND_IMPORT_STUB        1
#define     MEM_KIND_IMPORT_PTR         2

typedef struct {

    uint32_t start;
    uint32_t end;
    uint32_t prot;
    
    int kind;
    char name[64];

} MemRegion;

typedef struct {

    uint32_t r[16];
    uint32_t cpsr;
    
    uint8_t *mem;
    uint32_t mem_size;
    
    uint32_t last_pc;
    uint32_t last_insn;
    uint32_t last_mem_addr;
    
    int thumb;
    int running;
    
    int linux_abi;
    
    uint32_t tls;
    uint32_t clear_tid;
    
    uint8_t fd_flags[256];
    
    uint32_t brk_base, brk, stack_base;
    uint32_t exclusive_addr;
    
    int exclusive_valid;
    
    MemRegion regions[MAX_MEM_REGIONS];
    unsigned num_regions;

} Mach;

extern const char *program_name;

MemRegion *find_region (Mach *cpu, uint32_t addr, uint32_t size);
void map_region (Mach *cpu, uint32_t start, uint32_t size, uint32_t prot, int kind, const char *name);

int memory_has_prot (Mach *cpu, uint32_t addr, uint32_t size, uint32_t prot);
int memory_region_kind (Mach *cpu, uint32_t addr, uint32_t size);

void branch_to (Mach *cpu, uint32_t target);
void check_exec_fetch (Mach *cpu, uint32_t addr, uint32_t size);

void dump_fault (Mach*cpu, const char *what, uint32_t addr);
void stop_unsupported (Mach*cpu, const char *msg);

uint8_t read8 (Mach*cpu, uint32_t addr);
void write8 (Mach*cpu, uint32_t addr, uint8_t val);

uint32_t read32 (Mach*cpu, uint32_t addr);
void write32 (Mach*cpu, uint32_t addr, uint32_t val);

uint16_t read16 (Mach*cpu, uint32_t addr);
void write16 (Mach*cpu, uint32_t addr, uint16_t val);

uint32_t ror32 (uint32_t x, unsigned n);
int cond_passed (Mach*cpu, uint32_t cond);

void set_nz (Mach*cpu, uint32_t v);
void set_add_flags (Mach*cpu, uint32_t a, uint32_t b, uint32_t r);
void set_sub_flags (Mach*cpu, uint32_t a, uint32_t b, uint32_t r);

uint32_t get_reg (Mach*cpu, unsigned r);

void exec_thumb16 (Mach*cpu);
void exec_arm32 (Mach*cpu);

void linux_syscall (Mach *cpu);
void step (Mach*cpu);

void load_file (Mach *cpu, const char *path, int argc, char **argv, char **envp);

#endif      /* _ARMV7_H */

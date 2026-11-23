/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <limits.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "armv7.h"
#include    "report.h"

static uint64_t array_to_integer (unsigned char *arr, int size, int bigendian) {

    uint64_t value = 0;
    int i;
    
    if (bigendian) {
    
        int j;
        
        for (i = size, j = 0; i > 0; i--, j++) {
            value |= (uint64_t) arr[j] << (CHAR_BIT * (i - 1));
        }
    
    } else {
    
        for (i = 0; i < size; i++) {
            value |= (uint64_t) arr[i] << (CHAR_BIT * i);
        }
    
    }
    
    return value;

}

static void load_raw_image (Mach *cpu, unsigned char *file, size_t size) {

    if (LOAD_ADDR + size >= cpu->mem_size) {
    
        report_at (program_name, 0, REPORT_ERROR, "file too large");
        exit (EXIT_FAILURE);
    
    }
    
    memcpy(cpu->mem + LOAD_ADDR, file, size);
    
    map_region (cpu, LOAD_ADDR, (uint32_t) size, MEM_PROT_READ | MEM_PROT_WRITE | MEM_PROT_EXEC, MEM_KIND_NORMAL, "raw-image");
    cpu->r[R_PC] = LOAD_ADDR;

}

static int load_macho32 (Mach *cpu, unsigned char *file, size_t size) {

    uint32_t magic;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t off;
    uint32_t i;
    uint32_t entry = 0xffffffffu;
    uint32_t text_vmaddr = 0;
    uint32_t text_fileoff = 0;
    
    int have_text = 0;
    
    if (size < 28) {
        return 0;
    }
    
    magic = array_to_integer (file, 4, 0);
    
    if (magic != 0xfeedfaceu) {
        return 0;
    }
    
    ncmds = array_to_integer (file + 16, 4, 0);
    sizeofcmds = array_to_integer (file + 20, 4, 0);
    
    if (28U + sizeofcmds > size) {
    
        report_at (program_name, 0, REPORT_ERROR, "bad Mach-O load command size");
        exit (EXIT_FAILURE);
    
    }
    
    off = 28;
    
    for (i = 0; i < ncmds; i++) {
    
        uint32_t cmd;
        uint32_t cmdsize;
        
        if (off + 8 > size) {
        
            report_at (program_name, 0, REPORT_ERROR, "truncated Mach-O load command");
            exit (EXIT_FAILURE);
        
        }
        
        cmd = array_to_integer (file + off, 4, 0);
        cmdsize = array_to_integer (file + off + 4, 4, 0);
        
        if (cmdsize < 8 || off + cmdsize > size) {
        
            report_at (program_name, 0, REPORT_ERROR, "bad Mach-O load command");
            exit (EXIT_FAILURE);
        
        }
        
        if (cmd == 0x1) {               /* LC_SEGMENT */
        
            char segname[17];
            
            uint32_t vmaddr;
            uint32_t vmsize;
            uint32_t fileoff;
            uint32_t filesize;
            
            uint32_t initprot;
            uint32_t nsects;
            uint32_t prot = 0;
            uint32_t sec;
            
            if (cmdsize < 56) {
            
                report_at (program_name, 0, REPORT_ERROR, "bad LC_SEGMENT");
                exit (EXIT_FAILURE);
            
            }
            
            memcpy (segname, file + off + 8, 16);
            segname[16] = 0;
            
            vmaddr = array_to_integer (file + off + 24, 4, 0);
            vmsize = array_to_integer (file + off + 28, 4, 0);
            
            fileoff = array_to_integer (file + off + 32, 4, 0);
            filesize = array_to_integer (file + off + 36, 4, 0);
            
            initprot = array_to_integer (file + off + 44, 4, 0);
            nsects = array_to_integer (file + off + 48, 4, 0);
            
            if (initprot & 1) {
                prot |= MEM_PROT_READ;
            }
            
            if (initprot & 2) {
                prot |= MEM_PROT_WRITE;
            }
            
            if (initprot & 4) {
                prot |= MEM_PROT_EXEC;
            }
            
            if (!prot) {
                prot = MEM_PROT_READ;
            }
            
            if (fileoff + filesize > size) {
            
                report_at (program_name, 0, REPORT_ERROR, "segment outside file: %s", segname);
                exit (EXIT_FAILURE);
            
            }
            
            if (vmaddr + vmsize > cpu->mem_size) {
            
                report_at (program_name, 0, REPORT_ERROR, "segment outside emulator memory: %s vm=%08x size=%08x",
                        segname, vmaddr, vmsize);
                exit (EXIT_FAILURE);
            
            }
            
            if (filesize) {
                memcpy (cpu->mem + vmaddr, file + fileoff, filesize);
            }
            
            map_region (cpu, vmaddr, vmsize, prot, MEM_KIND_NORMAL, segname);
            
            for (sec = 0; sec < nsects; sec++) {
            
                uint32_t soff = off + 56 + sec * 68;
                
                char sectname[17];
                char fullname[64];
                
                uint32_t saddr;
                uint32_t ssize;
                
                int kind = MEM_KIND_NORMAL;
                
                if (soff + 68 > off + cmdsize) {
                    break;
                }
                
                memcpy (sectname, file + soff, 16);
                sectname[16] = 0;
                
                saddr = array_to_integer (file + soff + 32, 4, 0);
                ssize = array_to_integer (file + soff + 36, 4, 0);
                
                if (!ssize) {
                    continue;
                }
                
                if (strcmp (sectname, "__symbol_stub1") == 0 || strcmp (sectname, "__symbol_stub") == 0 ||
                        strcmp (sectname, "__picsymbolstub") == 0 || strcmp (sectname, "__picsymbolstub1") == 0) {
                    kind = MEM_KIND_IMPORT_STUB;
                } else if (strcmp (sectname, "__la_symbol_ptr") == 0 ||
                        strcmp (sectname, "__nl_symbol_ptr") == 0) {
                    kind = MEM_KIND_IMPORT_PTR;
                } else {
                    continue;
                }
                
                snprintf (fullname, sizeof (fullname), "%s,%s", segname, sectname);
                map_region (cpu, saddr, ssize, prot, kind, fullname);
            
            }
            
            if (strcmp (segname, "__TEXT") == 0) {
            
                text_vmaddr = vmaddr;
                text_fileoff = fileoff;
                
                have_text = 1;
            
            }
        
        } else if (cmd == 0x5) {        /* LC_UNIXTHREAD */
        
            uint32_t p = off + 8;
            
            while (p + 8 <= off + cmdsize) {
            
                uint32_t flavor = array_to_integer (file + p + 0, 4, 0);
                uint32_t count = array_to_integer (file + p + 4, 4, 0);
                
                uint32_t state = p + 8;
                uint32_t bytes = count * 4;
                
                if (state + bytes > off + cmdsize) {
                    break;
                }
                
                /* ARM_THREAD_STATE: r0-r12, sp, lr, pc, cpsr */
                if (flavor == 1 && count >= 17) {
                
                    entry = array_to_integer (file + state + 15 * 4, 4, 0);
                    break;
                
                }
                
                p = state + bytes;
            
            }
        
        } else if (cmd == 0x80000028U) {                    /* LC_MAIN */
        
            uint32_t entryoff;
            
            if (cmdsize >= 24 && have_text) {
            
                entryoff = array_to_integer (file + off + 8, 4, 0);
                entry = text_vmaddr + (entryoff - text_fileoff);
            
            }
        
        }
        
        off += cmdsize;
    
    }
    
    if (entry == 0xffffffffU) {
    
        report_at (program_name, 0, REPORT_ERROR, "Mach-O loaded, but no ARM entry point was found");
        exit (EXIT_FAILURE);
    
    }
    
    if (entry & 1) {
    
        cpu->thumb = 1;
        cpu->r[R_PC] = entry;
    
    } else {
    
        cpu->thumb = 0;
        cpu->r[R_PC] = entry;
    
    }
    
    return 1;

}

static uint32_t elf_mem_prot (uint32_t flags) {

    uint32_t prot = 0;
    
    if (flags & 4) {
        prot |= MEM_PROT_READ;
    }
    
    if (flags & 2) {
        prot |= MEM_PROT_WRITE;
    }
    
    if (flags & 1) {
        prot |= MEM_PROT_EXEC;
    }
    
    if (!prot) {
        prot = MEM_PROT_READ;
    }
    
    return prot;

}

static void elf_stack_error (void) {

    report_at (program_name, 0, REPORT_ERROR, "not enough memory for ELF startup stack");
    exit (EXIT_FAILURE);

}

static uint32_t elf_stack_string (Mach *cpu, uint32_t *sp, uint32_t bottom, const char *str) {

    size_t len = strlen (str) + 1;
    
    if (len > *sp - bottom) {
        elf_stack_error ();
    }
    
    *sp -= (uint32_t) len;
    memcpy (cpu->mem + *sp, str, len);
    
    return *sp;

}

static void elf_stack_aux (Mach *cpu, uint32_t *pos, uint32_t type, uint32_t value) {

    write32 (cpu, *pos, type);
    write32 (cpu, *pos + 4, value);
    
    *pos += 8;

}

static void elf_start_stack (Mach *cpu, int argc, char **argv, char **envp, uint32_t phdr, uint32_t phentsize, uint32_t phnum, uint32_t entry) {

    uint32_t bottom = 0, sp = cpu->mem_size;
    uint32_t pos, words, envc = 0, i;
    
    for (i = 0; i < cpu->num_regions; i++) {
    
        if (cpu->regions[i].end > bottom) {
            bottom = cpu->regions[i].end;
        }
    
    }
    
    cpu->brk_base = (bottom + 4095U) & ~4095U;
    cpu->brk = cpu->brk_base;
    cpu->stack_base = cpu->mem_size - 512 * 1024;
    
    if (cpu->brk_base > cpu->stack_base) {
        elf_stack_error ();
    }
    
    bottom = cpu->stack_base;
    
    while (envp && envp[envc]) {
        envc++;
    }
    
    if ((uint64_t) argc + envc + 3 + 22 > (sp - bottom) / 4) {
        elf_stack_error ();
    }
    
    words = (uint32_t) argc + envc + 3 + 22;
    
    for (i = 0; i < (uint32_t) argc; i++) {
    
        size_t len = strlen (argv[i]) + 1;
        
        if (len > sp - bottom) {
            elf_stack_error ();
        }
        
        sp -= (uint32_t) len;
    
    }
    
    for (i = 0; i < envc; i++) {
    
        size_t len = strlen (envp[i]) + 1;
        
        if (len > sp - bottom) {
            elf_stack_error ();
        }
        
        sp -= (uint32_t) len;
    
    }
    
    sp &= ~7U;
    
    if (sp < bottom || words > (sp - bottom) / 4) {
        elf_stack_error ();
    }
    
    sp = (sp - words * 4) & ~7U;
    
    if (sp < bottom) {
        elf_stack_error ();
    }
    
    cpu->r[R_SP] = sp;
    pos = sp;
    
    sp = cpu->mem_size;
    
    write32 (cpu, pos, (uint32_t) argc);
    pos += 4;
    
    for (i = 0; i < (uint32_t) argc; i++) {
    
        uint32_t addr = elf_stack_string (cpu, &sp, bottom, argv[i]);
        
        write32 (cpu, pos, addr);
        pos += 4;
    
    }
    
    write32 (cpu, pos, 0);
    pos += 4;
    
    for (i = 0; i < envc; i++) {
    
        uint32_t addr = elf_stack_string (cpu, &sp, bottom, envp[i]);
        
        write32 (cpu, pos, addr);
        pos += 4;
    
    }
    
    write32 (cpu, pos, 0);
    pos += 4;
    
    elf_stack_aux (cpu, &pos, 3, phdr);
    elf_stack_aux (cpu, &pos, 4, phentsize);
    elf_stack_aux (cpu, &pos, 5, phnum);
    elf_stack_aux (cpu, &pos, 6, 4096);
    elf_stack_aux (cpu, &pos, 7, 0);
    elf_stack_aux (cpu, &pos, 8, 0);
    elf_stack_aux (cpu, &pos, 9, entry);
    elf_stack_aux (cpu, &pos, 16, 0);
    elf_stack_aux (cpu, &pos, 23, 0);
    elf_stack_aux (cpu, &pos, 31, read32 (cpu, cpu->r[R_SP] + 4));
    elf_stack_aux (cpu, &pos, 0, 0);

}

static int load_elf32_arm (Mach *cpu, unsigned char *file, size_t size, int argc, char **argv, char **envp) {

    uint32_t phoff, phdr = 0;
    
    uint32_t type;
    uint32_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t flags;
    
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t i;
    
    int loaded = 0;
    (void) flags;
    
    if (size < 52) {
        return 0;
    }
    
    if (file[0] != 0x7f || file[1] != 'E' || file[2] != 'L' || file[3] != 'F') {
        return 0;
    }
    
    if (file[4] != 1) {
    
        report_at (program_name, 0, REPORT_ERROR, "ELF input is not 32-bit");
        exit (EXIT_FAILURE);
    
    }
    
    if (file[5] != 1) {
    
        report_at (program_name, 0, REPORT_ERROR, "ELF input is not little-endian");
        exit (EXIT_FAILURE);
    
    }
    
    if (file[6] != 1) {
    
        report_at (program_name, 0, REPORT_ERROR, "ELF input has an unsupported version");
        exit (EXIT_FAILURE);
    
    }
    
    type = array_to_integer (file + 16, 2, 0);
    machine = array_to_integer (file + 18, 2, 0);
    version = array_to_integer (file + 20, 4, 0);
    entry = array_to_integer (file + 24, 4, 0);
    phoff = array_to_integer (file + 28, 4, 0);
    flags = array_to_integer (file + 36, 4, 0);
    ehsize = array_to_integer (file + 40, 2, 0);
    phentsize = array_to_integer (file + 42, 2, 0);
    phnum = array_to_integer (file + 44, 2, 0);
    
    if (machine != 40) {                /* EM_ARM */
    
        report_at (program_name, 0, REPORT_ERROR, "ELF input is not ARM");
        exit (EXIT_FAILURE);
    
    }
    
    if (version != 1) {
    
        report_at (program_name, 0, REPORT_ERROR, "ELF input has an unsupported object version");
        exit (EXIT_FAILURE);
    
    }
    
    if (type != 2 && type != 3) {       /* ET_EXEC / ET_DYN */
    
        report_at (program_name, 0, REPORT_ERROR, "ELF input is not an executable/shared ARM image");
        exit (EXIT_FAILURE);
    
    }
    
    if (ehsize < 52) {
    
        report_at (program_name, 0, REPORT_ERROR, "bad ELF header size");
        exit (EXIT_FAILURE);
    
    }
    
    if (phnum && phentsize < 32) {
    
        report_at (program_name, 0, REPORT_ERROR, "bad ELF program-header size");
        exit (EXIT_FAILURE);
    
    }
    
    if (phnum && (uint64_t) phoff + (uint64_t) phnum * phentsize > size) {
    
        report_at (program_name, 0, REPORT_ERROR, "ELF program headers outside file");
        exit (EXIT_FAILURE);
    
    }
    
    for (i = 0; i < phnum; i++) {
    
        uint32_t poff = phoff + (uint32_t) i * phentsize;
        uint32_t p_type = array_to_integer (file + poff + 0, 4, 0);
        uint32_t p_offset = array_to_integer (file + poff + 4, 4, 0);
        uint32_t p_vaddr = array_to_integer (file + poff + 8, 4, 0);
        uint32_t p_filesz = array_to_integer (file + poff + 16, 4, 0);
        uint32_t p_memsz = array_to_integer (file + poff + 20, 4, 0);
        uint32_t p_flags = array_to_integer (file + poff + 24, 4, 0);
        uint32_t prot;
        
        if (p_type != 1) {              /* PT_LOAD */
            continue;
        }
        
        if (p_memsz < p_filesz) {
        
            report_at (program_name, 0, REPORT_ERROR, "bad ELF PT_LOAD size");
            exit (EXIT_FAILURE);
        
        }
        
        if ((uint64_t) p_offset + p_filesz > size) {
        
            report_at (program_name, 0, REPORT_ERROR, "ELF segment outside file");
            exit (EXIT_FAILURE);
        
        }
        
        if ((uint64_t) p_vaddr + p_memsz > cpu->mem_size) {
        
            report_at (program_name, 0, REPORT_ERROR, "ELF segment outside emulator memory: vm=%08x size=%08x", p_vaddr, p_memsz);
            exit (EXIT_FAILURE);
        
        }
        
        if (p_filesz) {
            memcpy (cpu->mem + p_vaddr, file + p_offset, p_filesz);
        }
        
        if (p_memsz > p_filesz) {
            memset (cpu->mem + p_vaddr + p_filesz, 0, p_memsz - p_filesz);
        }
        
        prot = elf_mem_prot (p_flags);
        map_region (cpu, p_vaddr, p_memsz, prot, MEM_KIND_NORMAL, "elf:PT_LOAD");
        
        if (phoff >= p_offset && (uint64_t) phoff + (uint64_t) phnum * phentsize <= (uint64_t) p_offset + p_filesz) {
            phdr = p_vaddr + (phoff - p_offset);
        }
        
        loaded = 1;
    
    }
    
    if (!loaded) {
    
        report_at (program_name, 0, REPORT_ERROR, "ELF has no loadable segments");
        exit (EXIT_FAILURE);
    
    }
    
    if (entry == 0) {
    
        report_at (program_name, 0, REPORT_ERROR, "ELF loaded, but entry point is zero");
        exit (EXIT_FAILURE);
    
    }
    
    cpu->linux_abi = 1;
    
    elf_start_stack (cpu, argc, argv, envp, phdr, phentsize, phnum, entry);
    branch_to (cpu, entry);
    
    return 1;

}

void load_file (Mach *cpu, const char *path, int argc, char **argv, char **envp) {

    FILE *fp;
    
    uint64_t le, be;
    size_t size;
    
    unsigned char *file;
    long fsize;
    
    if (!(fp = fopen (path, "rb"))) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to read '%s'", path);
        exit (EXIT_FAILURE);
    
    }
    
    fseek (fp, 0, SEEK_END);
    fsize = ftell (fp);
    fseek (fp, 0, SEEK_SET);
    
    if (fsize <= 0) {
    
        report_at (program_name, 0, REPORT_ERROR, "empty input '%s'", path);
        exit (EXIT_FAILURE);
    
    }
    
    size = (size_t) fsize;
    
    if (!(file = malloc (size))) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to allocate memory");
        exit (EXIT_FAILURE);
    
    }
    
    if (fread (file, 1, size, fp) != size) {
    
        fclose (fp);
        
        report_at (program_name, 0, REPORT_ERROR, "failed to read file '%s'", path);
        exit (EXIT_FAILURE);
    
    }
    
    fclose (fp);
    
    le = size >= 4 ? array_to_integer (file, 4, 0) : 0;
    be = size >= 4 ? array_to_integer (file, 4, 1) : 0;
    
    if (size >= 4 && file[0] == 'P' && file[1] == 'K') {
    
        report_at (program_name, 0, REPORT_ERROR, "input '%s' is a ZIP/IPA archive", path);
        exit (EXIT_FAILURE);
    
    }
    
    if (be == 0xCAFEBABEU || be == 0xCAFEBABFU) {
    
        report_at (program_name, 0, REPORT_ERROR, "input '%s' is a fat Mach-O binary", path);
        report_at (program_name, 0, REPORT_ERROR, "extract a thin ARMv7 slice first\n");
        
        exit (EXIT_FAILURE);
    
    }

    if (le == 0xFEEDFACFU || be == 0xFEEDFACFU) {
    
        report_at (program_name, 0, REPORT_ERROR, "input '%s' is a 64-bit Mach-O executable, not ARMv7", path);
        exit (EXIT_FAILURE);
    
    }
    
    if (!load_macho32 (cpu, file, size) && !load_elf32_arm (cpu, file, size, argc, argv, envp)) {
        load_raw_image (cpu, file, size);
    }
    
    free (file);

}

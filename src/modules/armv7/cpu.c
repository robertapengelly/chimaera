/******************************************************************************
 * @file            cpu.c
 *****************************************************************************/
#include    <sys/select.h>
#include    <sys/ioctl.h>
#include    <sys/stat.h>
#include    <sys/wait.h>

#include    <errno.h>
#include    <fcntl.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <termios.h>
#include    <time.h>
#include    <unistd.h>

#include    "armv7.h"
#include    "report.h"

void dump_fault (Mach *cpu, const char *what, uint32_t addr) {

    int i;
    
    report_at (program_name, 0, REPORT_ERROR, "%s out of bounds: %08x", what, addr);
    report_at (program_name, 0, REPORT_ERROR, "pc=%08x, insn=%08x, cpsr=%08x", cpu->last_pc, cpu->last_insn, cpu->cpsr);
    
    for (i = 0; i < 16; i++) {
        fprintf (stderr, "r%-2d=%08x%s", i, cpu->r[i], (i % 4) == 3 ? "\n" : " ");
    }

}

void stop_unsupported (Mach *cpu, const char *msg) {

    int i;
    
    report_at (program_name, 0, REPORT_ERROR, "%s", msg);
    fprintf (stderr, "pc=%08x insn=%08x cpsr=%08x mode=%s\n", cpu->last_pc, cpu->last_insn, cpu->cpsr, cpu->thumb ? "thumb" : "arm");
    fprintf (stderr, "last_mem_addr=%08x\n", cpu->last_mem_addr);
    
    for (i = 0; i < 16; i++) {
        fprintf (stderr, "r%-2d=%08x%s", i, cpu->r[i], (i % 4) == 3 ? "\n" : " ");
    }
    
    exit (EXIT_FAILURE);

}

uint8_t read8 (Mach *cpu, uint32_t addr) {

    cpu->last_mem_addr = addr;
    
    if (addr >= cpu->mem_size) {
    
        dump_fault (cpu, "read8", addr);
        exit (EXIT_FAILURE);
    
    }
    
    return cpu->mem[addr];

}

void write8 (Mach *cpu, uint32_t addr, uint8_t val) {

    cpu->exclusive_valid = 0;
    cpu->last_mem_addr = addr;
    
    if (addr >= cpu->mem_size) {
    
        dump_fault (cpu, "write8", addr);
        exit (EXIT_FAILURE);
    
    }
    
    cpu->mem[addr] = val;

}

uint16_t read16 (Mach *cpu, uint32_t addr) {

    cpu->last_mem_addr = addr;
    
    if (addr >= cpu->mem_size || cpu->mem_size - addr < 2) {
    
        dump_fault (cpu, "read16", addr);
        exit (EXIT_FAILURE);
    
    }
    
    return ((uint16_t) cpu->mem[addr + 0]) | ((uint16_t) cpu->mem[addr + 1] << 8);

}

void write16 (Mach *cpu, uint32_t addr, uint16_t val) {

    cpu->exclusive_valid = 0;
    cpu->last_mem_addr = addr;
    
    if (addr >= cpu->mem_size || cpu->mem_size - addr < 2) {
    
        dump_fault (cpu, "write16", addr);
        exit (EXIT_FAILURE);
    
    }
    
    cpu->mem[addr + 0] = val & 0xff;
    cpu->mem[addr + 1] = (val >> 8) & 0xff;

}

uint32_t read32 (Mach *cpu, uint32_t addr) {

    cpu->last_mem_addr = addr;
    
    if (addr >= cpu->mem_size || cpu->mem_size - addr < 4) {
    
        dump_fault (cpu, "read32", addr);
        exit (EXIT_FAILURE);
    
    }
    
    return ((uint32_t) cpu->mem[addr + 0]) | ((uint32_t) cpu->mem[addr + 1] << 8) | ((uint32_t) cpu->mem[addr + 2] << 16) | ((uint32_t) cpu->mem[addr + 3] << 24);

}

void write32 (Mach *cpu, uint32_t addr, uint32_t val) {

    cpu->exclusive_valid = 0;
    cpu->last_mem_addr = addr;
    
    if (addr >= cpu->mem_size || cpu->mem_size - addr < 4) {
    
        dump_fault (cpu, "write32", addr);
        exit (EXIT_FAILURE);
    
    }
    
    cpu->mem[addr + 0] = val & 0xff;
    cpu->mem[addr + 1] = (val >> 8) & 0xff;
    cpu->mem[addr + 2] = (val >> 16) & 0xff;
    cpu->mem[addr + 3] = (val >> 24) & 0xff;

}

uint32_t ror32 (uint32_t x, unsigned n) {

    n &= 31;
    
    if (!n) {
        return x;
    }
    
    return (x >> n) | (x << (32 - n));

}

int cond_passed (Mach *cpu, uint32_t cond) {

    int n = !!(cpu->cpsr & N_FLAG);
    int z = !!(cpu->cpsr & Z_FLAG);
    int c = !!(cpu->cpsr & C_FLAG);
    int v = !!(cpu->cpsr & V_FLAG);

    switch (cond) {
    
        case 0x0:
        
            return z;
        
        case 0x1:
        
            return !z;
        
        case 0x2:
        
            return c;
        
        case 0x3:
        
            return !c;
        
        case 0x4:
        
            return n;
        
        case 0x5:
        
            return !n;
        
        case 0x6:
        
            return v;
        
        case 0x7:
        
            return !v;
        
        case 0x8:
        
            return c && !z;
        
        case 0x9:
        
            return !c || z;
        
        case 0xa:
        
            return n == v;
        
        case 0xb:
        
            return n != v;
        
        case 0xc:
        
            return !z && n == v;
        
        case 0xd:
        
            return z || n != v;
        
        case 0xe:
        
            return 1;
        
        default:
        
            return 0;
    
    }

}

void set_nz (Mach *cpu, uint32_t v) {

    if (v & 0x80000000U) {
        cpu->cpsr |= N_FLAG;
    } else {
        cpu->cpsr &= ~N_FLAG;
    }
    
    if (v == 0) {
        cpu->cpsr |= Z_FLAG;
    } else {
        cpu->cpsr &= ~Z_FLAG;
    }

}

void set_add_flags (Mach *cpu, uint32_t a, uint32_t b, uint32_t r) {

    uint64_t wide = (uint64_t) a + (uint64_t) b;
    set_nz (cpu, r);
    
    if (wide >> 32) {
        cpu->cpsr |= C_FLAG;
    } else {
        cpu->cpsr &= ~C_FLAG;
    }
    
    if (((~(a ^ b)) & (a ^ r)) & 0x80000000U) {
        cpu->cpsr |= V_FLAG;
    } else {
        cpu->cpsr &= ~V_FLAG;
    }

}

void set_sub_flags (Mach *cpu, uint32_t a, uint32_t b, uint32_t r) {

    set_nz (cpu, r);
    
    if (a >= b) {
        cpu->cpsr |= C_FLAG;
    } else {
        cpu->cpsr &= ~C_FLAG;
    }
    
    if (((a ^ b) & (a ^ r)) & 0x80000000U) {
        cpu->cpsr |= V_FLAG;
    } else {
        cpu->cpsr &= ~V_FLAG;
    }

}

uint32_t get_reg (Mach *cpu, unsigned r) {

    if (r == R_PC) {
        return cpu->r[R_PC] + 8;
    }
    
    return cpu->r[r];

}

static int linux_range (Mach *cpu, uint32_t addr, uint32_t size) {
    return addr <= cpu->mem_size && size <= cpu->mem_size - addr;
}

static const char *linux_path (Mach *cpu, uint32_t addr) {

    uint32_t i;
    
    if (!addr || addr >= cpu->mem_size) {
        return NULL;
    }
    
    for (i = addr; i < cpu->mem_size; i++) {
    
        if (!cpu->mem[i]) {
            return (const char *) cpu->mem + addr;
        }
    
    }
    
    return NULL;

}

static int linux_exec_vector (Mach *cpu, uint32_t addr, char **vector, size_t capacity) {

    uint32_t ptr;
    size_t i;
    
    if (!addr) {
    
        vector[0] = NULL;
        return 0;
    
    }
    
    for (i = 0; i < capacity; i++, addr += 4) {
    
        if (!linux_range (cpu, addr, 4)) {
            return -EFAULT;
        }
        
        ptr = read32 (cpu, addr);
        
        if (!ptr) {
        
            vector[i] = NULL;
            return 0;
        
        }
        
        if (i + 1 == capacity) {
            return -E2BIG;
        }
        
        if (!(vector[i] = (char *) linux_path (cpu, ptr))) {
            return -EFAULT;
        }
    
    }
    
    return -E2BIG;

}

static int linux_execve (const char *path, char **argv, char **envp) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x3B), "D" (path), "S" (argv), "d" (envp) : "rcx", "r11", "memory");
#elif   defined (__I386__)
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x0B), "b" (path), "c" (argv), "d" (envp) : "memory");
#else
    ret = execve (path, argv, envp);
    
    if (ret < 0) {
        ret = -errno;
    }
#endif
    
    return ret;

}

static void linux_write64 (Mach *cpu, uint32_t addr, uint64_t value) {

    write32 (cpu, addr, (uint32_t) value);
    write32 (cpu, addr + 4, (uint32_t) (value >> 32));

}

static void linux_stat64 (Mach *cpu, uint32_t addr, struct stat *st) {

    memset (cpu->mem + addr, 0, 104);
    linux_write64 (cpu, addr, st->st_dev);
    
    write32 (cpu, addr + 12, (uint32_t) st->st_ino);
    write32 (cpu, addr + 16, (uint32_t) st->st_mode);
    write32 (cpu, addr + 20, (uint32_t) st->st_nlink);
    write32 (cpu, addr + 24, (uint32_t) st->st_uid);
    write32 (cpu, addr + 28, (uint32_t) st->st_gid);
    
    linux_write64 (cpu, addr + 32, st->st_rdev);
    linux_write64 (cpu, addr + 48, (uint64_t) st->st_size);
    
    write32 (cpu, addr + 56, (uint32_t) st->st_blksize);
    linux_write64 (cpu, addr + 64, (uint64_t) st->st_blocks);
    
    write32 (cpu, addr + 72, (uint32_t) st->st_atime);
    write32 (cpu, addr + 80, (uint32_t) st->st_mtime);
    write32 (cpu, addr + 88, (uint32_t) st->st_ctime);
    
    linux_write64 (cpu, addr + 96, st->st_ino);

}

static uint32_t linux_pid (void) {

    int pid;

#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (pid) : "a" (0x27) : "rcx", "r11", "memory");
#elif   defined (__I386__)
    __asm__ __volatile__ ("int 0x80" : "=a" (pid) : "a" (0x14));
#else
    pid = getpid ();
#endif

    return (uint32_t) pid;

}

static uint32_t linux_ppid (void) {

    char buf[256], *p;
    
    int fd = open ("/proc/self/status", O_RDONLY);
    ssize_t n;
    
    if (fd < 0) {
        return (uint32_t) -errno;
    }
    
    n = read (fd, buf, sizeof (buf) - 1);
    close (fd);
    
    if (n < 0) {
        return (uint32_t) -EIO;
    }
    
    buf[n] = 0;
    p = buf;
    
    while (*p && strncmp (p, "PPid:", 5)) {
        p++;
    }
    
    if (!*p) {
        return (uint32_t) -ENOSYS;
    }
    
    return (uint32_t) strtol (p + 5, NULL, 10);

}

static uint32_t linux_fcntl (Mach *cpu, int fd, uint32_t cmd, uint32_t arg) {

    struct stat st;
    int ret;
    
    if (fd < 0 || fd >= 256 || fstat (fd, &st) < 0) {
        return (uint32_t) -EBADF;
    }
    
    switch (cmd) {
    
        case 0:     case 1030:
        
            if (arg >= 256) {
                return (uint32_t) -EINVAL;
            }
            
            for (; arg < 256; arg++) {
            
                if (fstat ((int) arg, &st) < 0 && errno == EBADF) {
                
                    ret = dup2 (fd, (int) arg);
                    
                    if (ret < 0) {
                        return (uint32_t) -errno;
                    }
                    
                    cpu->fd_flags[ret] = cmd == 1030 ? 1 : 0;
                    return (uint32_t) ret;
                
                }
            
            }
            
            return (uint32_t) -EMFILE;
        
        case 1:
        
            return cpu->fd_flags[fd];
        
        case 2:
        
            cpu->fd_flags[fd] = arg & 1;
            return 0;
        
        case 3: {
        
            uint32_t flags;
            
            ret = fcntl (fd, F_GETFL, 0);
            
            if (ret < 0) {
                return (uint32_t) -errno;
            }
            
            flags = (ret & O_ACCMODE) == O_RDWR ? 2 : (ret & O_ACCMODE) == O_WRONLY ? 1 : 0;
            
            if (ret & O_APPEND) {
                flags |= 02000;
            }
            
            if (ret & O_NONBLOCK) {
                flags |= 04000;
            }
            
            return flags;
        
        }
        
        case 4: {
        
            int flags = 0;
            
            if (arg & 02000U) {
                flags |= O_APPEND;
            }
            
            if (arg & 04000U) {
                flags |= O_NONBLOCK;
            }
            
            ret = fcntl (fd, F_SETFL, flags);
            return ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
        
        }
        
        default:
        
            return (uint32_t) -ENOSYS;
    
    }

}

void linux_syscall (Mach *cpu) {

    switch (cpu->r[7]) {
    
        case 1:     case 248:
        
            if (cpu->clear_tid && linux_range (cpu, cpu->clear_tid, 4)) {
                write32 (cpu, cpu->clear_tid, 0);
            }
            
            exit ((int) (cpu->r[0] & 0xff));
        
        case 2: {
        
            int ret = fork ();
            
            if (ret == 0) {
            
                cpu->clear_tid = 0;
                cpu->exclusive_valid = 0;
            
            }
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 3:     case 4: {
        
            uint32_t addr = cpu->r[1], size = cpu->r[2];
            ssize_t ret;
            
            if (addr > cpu->mem_size || size > cpu->mem_size - addr) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            if (cpu->r[7] == 3) {
                ret = read ((int) cpu->r[0], cpu->mem + addr, size);
            } else {
                ret = write ((int) cpu->r[0], cpu->mem + addr, size);
            }
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 5: {
        
            uint32_t addr = cpu->r[0], flags = cpu->r[1];
            int oflag, ret;
            
            if (addr >= cpu->mem_size || !linux_path (cpu, addr)) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            switch (flags & 3) {
            
                case 0:
                
                    oflag = O_RDONLY;
                    break;
                
                case 1:
                
                    oflag = O_WRONLY;
                    break;
                
                case 2:
                
                    oflag = O_RDWR;
                    break;
                
                default:
                
                    cpu->r[0] = (uint32_t) -EINVAL;
                    return;
            
            }
            
            if (flags & 0100U) {
                oflag |= O_CREAT;
            }
            
            if (flags & 0200U) {
                oflag |= O_EXCL;
            }
            
            if (flags & 01000U) {
                oflag |= O_TRUNC;
            }
            
            if (flags & 02000U) {
                oflag |= O_APPEND;
            }
            
            if (flags & 04000U) {
                oflag |= O_NONBLOCK;
            }
            
            if (flags & ~(3U | 0100U | 0200U | 0400U | 01000U | 02000U | 04000U | 0x20000U | 0x80000U)) {
                cpu->r[0] = (uint32_t) -ENOSYS;
                return;
            }
            
            ret = open ((const char *) cpu->mem + addr, oflag, (mode_t) cpu->r[2]);
            
            if (ret >= 0 && ret < 256) {
                cpu->fd_flags[ret] = (flags & 0x80000U) ? 1 : 0;
            }
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 6: {
        
            int ret = close ((int) cpu->r[0]);
            
            if (ret == 0 && cpu->r[0] < 256) {
                cpu->fd_flags[cpu->r[0]] = 0;
            }
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 11: {
        
            const char *path = linux_path (cpu, cpu->r[0]);
            char *argv[128], *envp[128];
            
            int ret;
            
            if (!path) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            if ((ret = linux_exec_vector (cpu, cpu->r[1], argv, sizeof (argv) / sizeof (*argv))) || (ret = linux_exec_vector (cpu, cpu->r[2], envp, sizeof (envp) / sizeof (*envp)))) {
            
                cpu->r[0] = (uint32_t) ret;
                return;
            
            }
            
            cpu->r[0] = (uint32_t) linux_execve (path, argv, envp);
            return;
        
        }
        
        case 114: {
        
            uint32_t addr = cpu->r[1], options = cpu->r[2];
            int32_t pid = (int32_t) cpu->r[0];
            
            int status;
            pid_t ret;
            
            if ((addr && !linux_range (cpu, addr, 4)) || (cpu->r[3] && !linux_range (cpu, cpu->r[3], 72))) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            if (cpu->r[3] || pid == 0 || pid < -1 || (options & ~1U)) {
            
                cpu->r[0] = (uint32_t) -EINVAL;
                return;
            
            }
            
            ret = waitpid ((pid_t) pid, addr ? &status : NULL, (int) options);
            
            if (ret > 0 && addr) {
                write32 (cpu, addr, (uint32_t) status);
            }
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 20:    case 224:
        
            cpu->r[0] = linux_pid ();
            return;
        
        case 256:
        
            cpu->clear_tid = cpu->r[0];
            cpu->r[0] = linux_pid ();
            
            return;
        
        case 64:
        
            cpu->r[0] = linux_ppid ();
            return;
        
        case 55:    case 221:
        
            cpu->r[0] = linux_fcntl (cpu, (int) cpu->r[0], cpu->r[1], cpu->r[2]);
            return;
        
        case 45: {
        
            uint32_t end = cpu->r[0];
            
            uint32_t limit = cpu->stack_base;
            unsigned i;
            
            for (i = 0; i < cpu->num_regions; i++) {
            
                if (cpu->regions[i].start >= cpu->brk && cpu->regions[i].start < limit) {
                    limit = cpu->regions[i].start;
                }
            
            }
            
            if (end >= cpu->brk_base && end <= limit) {
            
                if (end > cpu->brk) {
                    memset (cpu->mem + cpu->brk, 0, end - cpu->brk);
                }
                
                cpu->brk = end;
            
            }
            
            cpu->r[0] = cpu->brk;
            return;
        
        }
        
        case 54: {
        
            uint32_t request = cpu->r[1], addr = cpu->r[2];
            int fd = (int) cpu->r[0], ret;
            
            if (request == 0x5413U) {
            
                struct winsize ws;
                
                if (addr > cpu->mem_size || 8 > cpu->mem_size - addr) {
                    cpu->r[0] = (uint32_t) -EFAULT;
                    return;
                }
                
                ret = ioctl (fd, TIOCGWINSZ, &ws);
                
                if (ret == 0) {
                
                    write16 (cpu, addr, ws.ws_row);
                    write16 (cpu, addr + 2, ws.ws_col);
                    write16 (cpu, addr + 4, ws.ws_xpixel);
                    write16 (cpu, addr + 6, ws.ws_ypixel);
                
                }
            
            } else if (request >= 0x5401U && request <= 0x5404U) {
            
                struct termios term;
                unsigned i;
                
                if (!linux_range (cpu, addr, 36)) {
                    cpu->r[0] = (uint32_t) -EFAULT;
                    return;
                }
                
                ret = tcgetattr (fd, &term);
                
                if (ret == 0) {
                
                    if (request == 0x5401U) {
                    
                        write32 (cpu, addr, term.c_iflag);
                        write32 (cpu, addr + 4, term.c_oflag);
                        write32 (cpu, addr + 8, term.c_cflag);
                        write32 (cpu, addr + 12, term.c_lflag);
                        write8 (cpu, addr + 16, term.c_line);
                        
                        for (i = 0; i < 19; i++) {
                            write8 (cpu, addr + 17 + i, term.c_cc[i]);
                        }
                    
                    } else {
                    
                        term.c_iflag = read32 (cpu, addr);
                        term.c_oflag = read32 (cpu, addr + 4);
                        term.c_cflag = read32 (cpu, addr + 8);
                        term.c_lflag = read32 (cpu, addr + 12);
                        term.c_line = read8 (cpu, addr + 16);
                        
                        for (i = 0; i < 19; i++) {
                            term.c_cc[i] = read8 (cpu, addr + 17 + i);
                        }

#if     defined (__AMD64__) || defined (__I386__)
                        ret = tcsetattr (fd, TCSANOW, &term);
#else
                        ret = tcsetattr (fd, (int) request - 0x5402, &term);
#endif

                    }
                
                }
            
            } else {
            
                cpu->r[0] = (uint32_t) -ENOTTY;
                return;
            
            }
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 19:    case 140: {
        
            int fd = (int) cpu->r[0];
            
            int64_t offset;
            uint32_t whence;
            
            off_t ret;
            
            if (cpu->r[7] == 140) {
            
                offset = (int64_t) (((uint64_t) cpu->r[1] << 32) | cpu->r[2]);
                whence = cpu->r[4];
                
                if (!linux_range (cpu, cpu->r[3], 8)) {
                
                    cpu->r[0] = (uint32_t) -EFAULT;
                    return;
                
                }
            
            } else {
            
                offset = (int32_t) cpu->r[1];
                whence = cpu->r[2];
            
            }
            
            if (offset != (int64_t) (int32_t) offset) {
            
                cpu->r[0] = (uint32_t) -EOVERFLOW;
                return;
            
            }
            
            ret = lseek (fd, (off_t) offset, (int) whence);
            
            if (ret < 0) {
                cpu->r[0] = (uint32_t) -errno;
            } else if (cpu->r[7] == 140) {
            
                linux_write64 (cpu, cpu->r[3], (uint64_t) ret);
                cpu->r[0] = 0;
            
            } else {
                cpu->r[0] = (uint32_t) ret;
            }
            
            return;
        
        }
        
        case 146: {
        
            uint32_t addr = cpu->r[1], count = cpu->r[2], i, total = 0;
            ssize_t ret;
            
            if (addr > cpu->mem_size || count > (cpu->mem_size - addr) / 8 || count > 1024U) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            for (i = 0; i < count; i++) {
            
                uint32_t ptr = read32 (cpu, addr + i * 8);
                uint32_t size = read32 (cpu, addr + i * 8 + 4);
                
                if (ptr > cpu->mem_size || size > cpu->mem_size - ptr) {
                
                    cpu->r[0] = (uint32_t) -EFAULT;
                    return;
                
                }
                
                ret = write ((int) cpu->r[0], cpu->mem + ptr, size);
                
                if (ret < 0) {
                
                    cpu->r[0] = total ? total : (uint32_t) -errno;
                    return;
                
                }
                
                total += (uint32_t) ret;
                
                if ((uint32_t) ret < size) {
                    break;
                }
            
            }
            
            cpu->r[0] = total;
            return;
        
        }
        
        case 142:   case 335:   case 413: {
        
            uint32_t nfds = cpu->r[0], addr = cpu->r[4];
            uint32_t size = ((nfds + 31U) / 32U) * 4, i, fd;
            
            fd_set sets[3];
            fd_set *setp[3];
            
            struct timeval tv, *timeout = NULL;
            int ret;
            
            if (nfds > FD_SETSIZE) {
            
                cpu->r[0] = (uint32_t) -EINVAL;
                return;
            
            }
            
            if (cpu->r[7] != 142 && cpu->r[5]) {
            
                if (!linux_range (cpu, cpu->r[5], 8)) {
                
                    cpu->r[0] = (uint32_t) -EFAULT;
                    return;
                
                }
                
                if (read32 (cpu, cpu->r[5]) && (!linux_range (cpu, read32 (cpu, cpu->r[5]), 8) || read32 (cpu, cpu->r[5] + 4) != 8)) {
                
                    cpu->r[0] = (uint32_t) -EINVAL;
                    return;
                
                }
            
            }
            
            for (i = 0; i < 3; i++) {
            
                uint32_t ptr = cpu->r[i + 1];
                setp[i] = NULL;
                
                if (ptr) {
                
                    if (!linux_range (cpu, ptr, size)) {
                    
                        cpu->r[0] = (uint32_t) -EFAULT;
                        return;
                    
                    }
                    
                    setp[i] = &sets[i];
                    FD_ZERO (setp[i]);
                    
                    for (fd = 0; fd < nfds; fd++) {
                    
                        if (read32 (cpu, ptr + (fd / 32) * 4) & (1U << (fd % 32))) {
                            FD_SET (fd, setp[i]);
                        }
                    
                    }
                
                }
            
            }
            
            if (addr) {
            
                uint32_t sec, nsec;
                
                if (!linux_range (cpu, addr, cpu->r[7] == 413 ? 16 : 8)) {
                
                    cpu->r[0] = (uint32_t) -EFAULT;
                    return;
                
                }
                
                sec = read32 (cpu, addr);
                nsec = read32 (cpu, addr + (cpu->r[7] == 413 ? 8 : 4));
                
                if (cpu->r[7] == 142) {
                
                    if (nsec >= 1000000U) {
                    
                        cpu->r[0] = (uint32_t) -EINVAL;
                        return;
                    
                    }
                    
                    nsec *= 1000U;
                
                }
                
                if ((sec & 0x80000000U) || nsec >= 1000000000U || (cpu->r[7] == 413 && (read32 (cpu, addr + 4) || read32 (cpu, addr + 12)))) {
                
                    cpu->r[0] = (uint32_t) -EINVAL;
                    return;
                
                }
                
                tv.tv_sec = sec;
                tv.tv_usec = (nsec + 999U) / 1000U;
                
                if (tv.tv_usec == 1000000) {
                
                    if (sec == 0x7fffffffU) {
                    
                        cpu->r[0] = (uint32_t) -EOVERFLOW;
                        return;
                    
                    }
                    
                    tv.tv_sec++;
                    tv.tv_usec = 0;
                
                }
                
                timeout = &tv;
            
            }
            
            ret = select ((int) nfds, setp[0], setp[1], setp[2], timeout);
            
            if (ret >= 0) {
            
                if (addr && cpu->r[7] == 142) {
                
                    write32 (cpu, addr, (uint32_t) tv.tv_sec);
                    write32 (cpu, addr + 4, (uint32_t) tv.tv_usec);
                
                }
                
                for (i = 0; i < 3; i++) {
                
                    uint32_t ptr = cpu->r[i + 1];
                    
                    if (ptr) {
                    
                        memset (cpu->mem + ptr, 0, size);
                        
                        for (fd = 0; fd < nfds; fd++) {
                        
                            if (FD_ISSET (fd, setp[i])) {
                            
                                uint32_t pos = ptr + (fd / 32) * 4;
                                write32 (cpu, pos, read32 (cpu, pos) | (1U << (fd % 32)));
                            
                            }
                        
                        }
                    
                    }
                
                }
            
            }
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 168: {
        
            uint32_t addr = cpu->r[0], count = cpu->r[1], i;
            
            struct stat st;
            int ready = 0;
            
            if (addr > cpu->mem_size || count > (cpu->mem_size - addr) / 8) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            if (cpu->r[2]) {
            
                cpu->r[0] = (uint32_t) -ENOSYS;
                return;
            
            }
            
            for (i = 0; i < count; i++, addr += 8) {
            
                int fd = (int) read32 (cpu, addr);
                
                uint16_t events = read16 (cpu, addr + 4);
                uint16_t revents = 0;
                
                if (fd >= 0) {
                
                    if (fstat (fd, &st) < 0) {
                    
                        if (errno != EBADF) {
                        
                            cpu->r[0] = (uint32_t) -errno;
                            return;
                        
                        }
                        
                        revents = 0x20;
                    
                    } else if (events) {
                        cpu->r[0] = (uint32_t) -ENOSYS;
                        return;
                    }
                
                }
                
                write16 (cpu, addr + 6, revents);
                
                if (revents) {
                    ready++;
                }
            
            }
            
            cpu->r[0] = (uint32_t) ready;
            return;
        
        }
        
        case 91: {
        
            uint32_t addr = cpu->r[0], size = cpu->r[1], end;
            unsigned i;
            
            if ((addr & 4095U) || !size || addr < cpu->brk_base || addr > cpu->stack_base || size > cpu->stack_base - addr) {
            
                cpu->r[0] = (uint32_t) -EINVAL;
                return;
            
            }
            
            end = addr + ((size + 4095U) & ~4095U);
            
            for (i = 0; i < cpu->num_regions; i++) {
            
                MemRegion *r = &cpu->regions[i];
                
                if (addr >= r->end || end <= r->start) {
                    continue;
                }
                
                if (addr > r->start && end < r->end) {
                
                    if (cpu->num_regions >= MAX_MEM_REGIONS) {
                    
                        cpu->r[0] = (uint32_t) -ENOMEM;
                        return;
                    
                    }
                    
                    map_region (cpu, end, r->end - end, r->prot, r->kind, r->name);
                    r->end = addr;
                
                } else if (addr > r->start) {
                    r->end = addr;
                } else if (end < r->end) {
                    r->start = end;
                } else {
                
                    cpu->num_regions--;
                    
                    memcpy (&cpu->regions[i], &cpu->regions[cpu->num_regions], sizeof (MemRegion));
                    i--;
                
                }
            
            }
            
            cpu->r[0] = 0;
            return;
        
        }
        
        case 192: {
        
            uint32_t size = cpu->r[1], prot = cpu->r[2], flags = cpu->r[3];
            uint32_t addr, bottom = (cpu->brk + 4095U) & ~4095U;
            
            unsigned i;
            
            if (!size || size > cpu->stack_base - bottom) {
            
                cpu->r[0] = (uint32_t) -ENOMEM;
                return;
            
            }
            
            if ((flags & 0x23U) != 0x22U) {
            
                cpu->r[0] = (uint32_t) -ENOSYS;
                return;
            
            }
            
            size = (size + 4095U) & ~4095U;
            
            if (flags & 0x10U) {
            
                addr = cpu->r[0];
                
                if ((addr & 4095U) || addr < cpu->brk_base || addr > cpu->stack_base || size > cpu->stack_base - addr) {
                
                    cpu->r[0] = (uint32_t) -EINVAL;
                    return;
                
                }
                
                for (i = 0; i < cpu->num_regions; i++) {
                
                    if (addr < cpu->regions[i].end && addr + size > cpu->regions[i].start) {
                    
                        cpu->r[0] = (uint32_t) -ENOSYS;
                        return;
                    
                    }
                
                }
                
                if (cpu->num_regions >= MAX_MEM_REGIONS) {
                
                    cpu->r[0] = (uint32_t) -ENOMEM;
                    return;
                
                }
                
                memset (cpu->mem + addr, 0, size);
                
                map_region (cpu, addr, size, prot & 7U, MEM_KIND_NORMAL, "linux:mmap");
                cpu->r[0] = addr;
                
                return;
            
            }
            
            addr = cpu->stack_base - size;
            
            while (addr >= bottom) {
            
                for (i = 0; i < cpu->num_regions; i++) {
                
                    if (addr < cpu->regions[i].end && addr + size > cpu->regions[i].start) {
                        break;
                    }
                
                }
                
                if (i == cpu->num_regions) {
                
                    if (cpu->num_regions >= MAX_MEM_REGIONS) {
                        break;
                    }
                    
                    memset (cpu->mem + addr, 0, size);
                    
                    map_region (cpu, addr, size, prot & 7U, MEM_KIND_NORMAL, "linux:mmap");
                    cpu->r[0] = addr;
                    
                    return;
                
                }
                
                if (cpu->regions[i].start < bottom + size) {
                    break;
                }
                
                addr = cpu->regions[i].start - size;
            
            }
            
            cpu->r[0] = (uint32_t) -ENOMEM;
            return;
        
        }
        
        case 122: {
        
            uint32_t addr = cpu->r[0];
            
            if (addr > cpu->mem_size || 390 > cpu->mem_size - addr) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            memset (cpu->mem + addr, 0, 390);
            
            strcpy ((char *) cpu->mem + addr, "ChimaeraOS");
            strcpy ((char *) cpu->mem + addr + 65, "chimaera");
            strcpy ((char *) cpu->mem + addr + 130, "0.0.0");
            strcpy ((char *) cpu->mem + addr + 195, "ARM Linux compatibility");
            strcpy ((char *) cpu->mem + addr + 260, "armv7l");
            
            cpu->r[0] = 0;
            return;
        
        }
        
        case 125:
        
            cpu->r[0] = (uint32_t) -ENOSYS;
            return;
        
        case 24:    case 47:    case 49:    case 50:
        case 199:   case 200:   case 201:   case 202:
        
            cpu->r[0] = 0;
            return;
        
        case 85: {
        
            const char *path = linux_path (cpu, cpu->r[0]);
            uint32_t addr = cpu->r[1], size = cpu->r[2];
            
            struct stat st;
            
            if (!size || size > 0x7FFFFFFFU) {
            
                cpu->r[0] = (uint32_t) -EINVAL;
                return;
            
            }
            
            if (!path || !linux_range (cpu, addr, size)) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            if (lstat (path, &st) < 0) {
                cpu->r[0] = (uint32_t) -errno;
            } else if ((st.st_mode & S_IFMT) == S_IFLNK) {
                cpu->r[0] = (uint32_t) -ENOSYS;
            } else {
                cpu->r[0] = (uint32_t) -EINVAL;
            }
            
            return;
        
        }
        
        case 183: {
        
            uint32_t addr = cpu->r[0], size = cpu->r[1];
            
            if (!linux_range (cpu, addr, size)) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            if (!size) {
            
                cpu->r[0] = (uint32_t) -EINVAL;
                return;
            
            }
            
            if (!getcwd ((char *) cpu->mem + addr, size)) {
            
                cpu->r[0] = (uint32_t) -errno;
                return;
            
            }
            
            cpu->r[0] = (uint32_t) strlen ((char *) cpu->mem + addr) + 1;
            return;
        
        }
        
        case 12:    case 33: {
        
            const char *path = linux_path (cpu, cpu->r[0]);
            int ret;
            
            if (!path) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            ret = cpu->r[7] == 12 ? chdir (path) : access (path, (int) cpu->r[1]);
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            
            return;
        
        }
        
        case 439: {
        
            const char *path = linux_path (cpu, cpu->r[1]);
            uint32_t mode = cpu->r[2], flags = cpu->r[3];
            
            int ret;
            
            if ((mode & ~7U) || (flags & ~(0x100U | 0x200U | 0x1000U))) {
            
                cpu->r[0] = (uint32_t) -EINVAL;
                return;
            
            }
            
            if (!path) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            if (flags & (0x100U | 0x1000U)) {
            
                cpu->r[0] = (uint32_t) -ENOSYS;
                return;
            
            }
            
            if (path[0] != '/' && (int32_t) cpu->r[0] != -100) {
            
                cpu->r[0] = (uint32_t) -ENOSYS;
                return;
            
            }
            
            ret = access (path, (int) mode);
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 42:    case 359: {
        
            uint32_t addr = cpu->r[0];
            uint32_t flags = cpu->r[7] == 359 ? cpu->r[1] : 0;
            
            int fds[2];
            
            if (flags & ~(04000U | 0x80000U)) {
            
                cpu->r[0] = (uint32_t) -EINVAL;
                return;
            
            }
            
            if (flags) {
            
                cpu->r[0] = (uint32_t) -ENOSYS;
                return;
            
            }
            
            if (!addr || !linux_range (cpu, addr, 8)) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            if (pipe (fds) < 0) {
            
                cpu->r[0] = (uint32_t) -errno;
                return;
            
            }
            
            if (fds[0] >= 0 && fds[0] < 256) {
                cpu->fd_flags[fds[0]] = 0;
            }
            
            if (fds[1] >= 0 && fds[1] < 256) {
                cpu->fd_flags[fds[1]] = 0;
            }
            
            write32 (cpu, addr, (uint32_t) fds[0]);
            write32 (cpu, addr + 4, (uint32_t) fds[1]);
            
            cpu->r[0] = 0;
            return;
        
        }
        
        case 41:    case 63: {
        
            int ret = cpu->r[7] == 41 ? dup ((int) cpu->r[0]) : dup2 ((int) cpu->r[0], (int) cpu->r[1]);
            
            if (ret >= 0 && ret < 256 && (cpu->r[7] == 41 || cpu->r[0] != cpu->r[1])) {
                cpu->fd_flags[ret] = 0;
            }
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 195:   case 196:   case 197: {
        
            uint32_t addr = cpu->r[1];
            
            struct stat st;
            int ret;
            
            if (!linux_range (cpu, addr, 104)) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            if (cpu->r[7] == 197) {
                ret = fstat ((int) cpu->r[0], &st);
            } else {
            
                const char *path = linux_path (cpu, cpu->r[0]);
                
                if (!path) {
                
                    cpu->r[0] = (uint32_t) -EFAULT;
                    return;
                
                }
                
                ret = cpu->r[7] == 195 ? stat (path, &st) : lstat (path, &st);
            
            }
            
            if (ret == 0) {
                linux_stat64 (cpu, addr, &st);
            }
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 263:   case 403: {
        
            uint32_t addr = cpu->r[1], size = cpu->r[7] == 403 ? 16 : 8;
            
            struct timespec ts;
            int ret;
            
            if (addr > cpu->mem_size || size > cpu->mem_size - addr) {
            
                cpu->r[0] = (uint32_t) -EFAULT;
                return;
            
            }
            
            ret = clock_gettime ((clockid_t) cpu->r[0], &ts);
            
            if (ret == 0) {
            
                write32 (cpu, addr, (uint32_t) ts.tv_sec);
                
                if (size == 16) {
                
                    write32 (cpu, addr + 4, (uint32_t) ((uint64_t) ts.tv_sec >> 32));
                    write32 (cpu, addr + 8, (uint32_t) ts.tv_nsec);
                    write32 (cpu, addr + 12, 0);
                
                } else {
                    write32 (cpu, addr + 4, (uint32_t) ts.tv_nsec);
                }
            
            }
            
            cpu->r[0] = ret < 0 ? (uint32_t) -errno : (uint32_t) ret;
            return;
        
        }
        
        case 57:    case 132:   case 174:   case 175:
        case 191:   case 334:   case 338:   case 369:
        case 397:
        
            cpu->r[0] = (uint32_t) -ENOSYS;
            return;
        
        case 0xf0005:
        
            cpu->tls = cpu->r[0];
            cpu->r[0] = 0;
            
            return;
        
        default:
        
            report_at (program_name, 0, REPORT_ERROR, "unimplemented ARM Linux syscall %u at %08x", cpu->r[7], cpu->last_pc);
            exit (EXIT_FAILURE);
    
    }

}

void step (Mach *cpu) {

    if (cpu->thumb || (cpu->r[R_PC] & 1u)) {
    
        cpu->thumb = 1;
        
        exec_thumb16 (cpu);
        return;
    
    }
    
    exec_arm32 (cpu);

}

void map_region (Mach *cpu, uint32_t start, uint32_t size, uint32_t prot, int kind, const char *name) {

    MemRegion *r;
    uint32_t end;
    
    if (!size) {
        return;
    }
    
    end = start + size;
    
    if (end < start || end > cpu->mem_size) {
    
        report_at (program_name, 0, REPORT_ERROR, "memory region outside emulator memory: %08x size=%08x", start, size);
        exit (EXIT_FAILURE);
    
    }
    
    if (cpu->num_regions >= MAX_MEM_REGIONS) {
    
        report_at (program_name, 0, REPORT_ERROR, "too many memory regions");
        exit (EXIT_FAILURE);
    
    }
    
    r = &cpu->regions[cpu->num_regions++];
    r->start = start;
    r->end = end;
    r->prot = prot;
    r->kind = kind;
    
    if (name) {
    
        size_t n = 0;
        
        while (n + 1 < sizeof (r->name) && name[n]) {
        
            r->name[n] = name[n];
            n++;
        
        }
        
        r->name[n] = 0;
    
    } else {
        r->name[0] = 0;
    }

}

MemRegion *find_region (Mach *cpu, uint32_t addr, uint32_t size) {

    int i;
    uint32_t end;
    
    if (!size) {
        size = 1;
    }
    
    end = addr + size;
    
    if (end < addr) {
        return 0;
    }
    
    for (i = (int) cpu->num_regions - 1; i >= 0; i--) {
    
        MemRegion *r = &cpu->regions[i];
        
        if (addr >= r->start && end <= r->end) {
            return r;
        }
    
    }
    
    return 0;

}

int memory_has_prot (Mach *cpu, uint32_t addr, uint32_t size, uint32_t prot) {

    MemRegion *r = find_region (cpu, addr, size);
    
    if (!r) {
        return 0;
    }
    
    return (r->prot & prot) == prot;

}

int memory_region_kind (Mach *cpu, uint32_t addr, uint32_t size) {

    MemRegion *r = find_region (cpu, addr, size);
    
    if (!r) {
        return MEM_KIND_NORMAL;
    }
    
    return r->kind;

}

void check_exec_fetch (Mach *cpu, uint32_t addr, uint32_t size) {

    MemRegion *r = find_region (cpu, addr, size);
    
    if (!r) {
        stop_unsupported (cpu, "attempted to execute unmapped memory");
    }
    
    if (!(r->prot & MEM_PROT_EXEC)) {
        stop_unsupported (cpu, "attempted to execute non-executable memory");
    }
    
    if (r->kind == MEM_KIND_IMPORT_STUB) {
        stop_unsupported (cpu, "hit Mach-O import stub; dynamic linker/iOS syscalls are not implemented");
    }

}

void branch_to (Mach *cpu, uint32_t target) {

    uint32_t pc = target & ~1U;
    MemRegion *r = find_region (cpu, pc, 2);
    
    if (!r) {
        stop_unsupported (cpu, "branch target is unmapped; unresolved import/native stub is not implemented");
    }
    
    if (!(r->prot & MEM_PROT_EXEC)) {
        stop_unsupported (cpu, "branch target is not executable; unresolved import/native stub is not implemented");
    }
    
    if (r->kind == MEM_KIND_IMPORT_STUB) {
        stop_unsupported (cpu, "hit Mach-O import stub; dynamic linker/iOS syscalls are not implemented");
    }
    
    cpu->thumb = target & 1U;
    cpu->r[R_PC] = pc;
    
    if (cpu->thumb) {
        cpu->r[R_PC] |= 1U;
    }

}

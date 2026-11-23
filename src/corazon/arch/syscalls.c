/******************************************************************************
 * @file            syscalls.c
 *****************************************************************************/
#include    <corazon/tasking/irq.h>
#include    <corazon/tasking/process.h>
#include    <corazon/vfs/pipe.h>
#include    <corazon/regs.h>
#include    <corazon/timer.h>

#include    <sys/signal_defs.h>
#include    <sys/types.h>

#include    <errno.h>
#include    <stdint.h>
#include    <time.h>

extern void *mmap (void *addr, size_t length, int prot, int flags, int fd, uintptr_t offset);

extern ssize_t kread (int __fd, void *__buf, size_t __nbytes);
extern ssize_t kwrite (int __fd, void *__buf, size_t __nbytes);

extern int kopen (const char *__file, int __oflag);
extern int munmap (void *addr, size_t length);

extern off_t kseek (int __fd, off_t __offset, int __whence);
extern int kclose (int __fd);

extern int kdup (int __fd);
extern int kdup2 (int __fd, int __newfd);

extern ssize_t kgetdents64 (int fd, uintptr_t address, size_t count);
extern int kftruncate (int __fd, off_t __length);
extern int krename (uintptr_t old_path, uintptr_t new_path);

extern int kmkdir (uintptr_t path, mode_t mode);
extern int kremove (uintptr_t path, int directory);

extern int kaccess (uintptr_t path, int mode);
extern int kutime (uintptr_t path, uintptr_t times, int compat32);

extern int kioctl (int __fd, unsigned long request, void *arg);
extern int kfstat (int fd, uintptr_t statbuf);
extern int kstat (uintptr_t path, uintptr_t statbuf);

extern int kfstatfs (int fd, uintptr_t statbuf, int compat32);
extern int kstatfs (uintptr_t path, uintptr_t statbuf, int compat32);
extern int kselect (int nfds, uintptr_t readfds, uintptr_t writefds, uintptr_t exceptfds, uintptr_t timeout, int compat32);

extern intptr_t kgetcwd (uintptr_t address, size_t size);
extern int kchdir (uintptr_t path);

extern int cmos_get_unix_time (uint64_t *seconds);
extern void schedule (struct regs *regs);

static intptr_t syscall_time (uintptr_t address, int compat32) {

    uintptr_t pd = thread_get_current ()->owner->pd;
    uint64_t seconds;
    
    if (!cmos_get_unix_time (&seconds)) {
        return -EIO;
    }
    
#if     defined (__AMD64__)
    if (!compat32) {
    
        if (address && !mmu_copy_user (pd, address, &seconds, sizeof (seconds), 1)) {
            return -EFAULT;
        }
        
        return (intptr_t) seconds;
    
    }
#else
    (void) compat32;
#endif
    
    if (seconds > UINT32_MAX) {
        return -EOVERFLOW;
    }
    
    if (address) {
    
        uint32_t seconds32 = (uint32_t) seconds;
        
        if (!mmu_copy_user (pd, address, &seconds32, sizeof (seconds32), 1)) {
            return -EFAULT;
        }
    
    }
    
    return (intptr_t) seconds;

}

static intptr_t syscall_clock_gettime (int clock_id, uintptr_t address, int compat32) {

    uintptr_t pd = thread_get_current ()->owner->pd;
    
    uint64_t seconds, microseconds;
    uint32_t nanoseconds = 0;
    
    int32_t result32[2];
    
    if (clock_id == CLOCK_REALTIME) {
    
        if (!cmos_get_unix_time (&seconds)) {
            return -EIO;
        }
    
    } else if (clock_id == CLOCK_MONOTONIC) {
    
        microseconds = get_uptime_microseconds ();
        seconds = microseconds / 1000000ULL;
        nanoseconds = (uint32_t) (microseconds % 1000000ULL) * 1000;
    
    } else {
        return -EINVAL;
    }
    
    if (!address) {
        return -EFAULT;
    }
    
#if     defined (__AMD64__)
    if (!compat32) {
    
        uint64_t result64[2];
        
        result64[0] = seconds;
        result64[1] = nanoseconds;
        
        if (!mmu_copy_user (pd, address, result64, sizeof (result64), 1)) {
            return -EFAULT;
        }
        
        return 0;
    
    }
#else
    (void) compat32;
#endif
    
    if (seconds > 0x7FFFFFFFULL) {
        return -EOVERFLOW;
    }
    
    result32[0] = (int32_t) seconds;
    result32[1] = (int32_t) nanoseconds;
    
    if (!mmu_copy_user (pd, address, result32, sizeof (result32), 1)) {
        return -EFAULT;
    }
    
    return 0;

}

static intptr_t syscall_nanosleep (const struct timespec *req, struct timespec *rem, uint64_t *delay) {

    struct timespec requested;
    
    if (!req) {
        return -EFAULT;
    }
    
    requested.tv_sec = req->tv_sec;
    requested.tv_nsec = req->tv_nsec;
    
    if (requested.tv_sec < 0 || requested.tv_nsec < 0 || requested.tv_nsec >= 1000000000) {
        return -EINVAL;
    }
    
    if ((uint64_t) requested.tv_sec > (UINT64_MAX - 1000ULL) / 1000ULL) {
        return -EINVAL;
    }
    
    *delay = ((uint64_t) requested.tv_sec * 1000ULL) + (((uint64_t) requested.tv_nsec + 999999ULL) / 1000000ULL);
    
    (void) rem;
    return 0;

}

#if     defined (__AMD64__)
__attribute__ ((packed)) struct mmap_args32 {
    uint32_t addr, length, prot, flags, fd, offset;
};

__attribute__ ((packed)) struct timespec32 {
    int32_t tv_sec, tv_nsec;
};

static int handle_syscall_compat32 (struct regs *r) {

    switch ((uint32_t) r->rax) {
    
        case 0x01 /* exit */:   case 0xFC /* exit_group */: {
        
            process_exit ((int32_t) r->rbx, r);
            break;
        
        }
        
        case 0x02: {
        
            uintptr_t pd = mmu_clone_address_space (thread_get_current ()->owner->pd);
            
            if (!pd) {
                r->rax = (uint32_t) -ENOMEM;
            } else {
            
                r->rax = (uint32_t) process_fork_from_clone (r, pd);
                
                if ((int32_t) r->rax < 0) {
                    mmu_destroy_address_space (pd);
                }
            
            }
            
            break;
        
        }
        
        case 0x03: {
        
            r->rax = (uint32_t) kread ((uint32_t) r->rbx, (void *) (uintptr_t) (uint32_t) r->rcx, (uint32_t) r->rdx);
            
            if ((int32_t) r->rax == -EAGAIN && (thread_get_current ()->state == TS_WAITIO || thread_get_current ()->state == TS_PIPE)) {
            
                r->rax = 0;
                schedule (r);
            
            }
            
            break;
        
        }
        
        case 0x04: {
        
            r->rax = (uint32_t) kwrite ((uint32_t) r->rbx, (void *) (uintptr_t) (uint32_t) r->rcx, (uint32_t) r->rdx);
            
            if (thread_get_current ()->state == TS_PIPE || thread_get_current ()->pending_signal == SIGPIPE) {
                schedule (r);
            }
            
            break;
        
        }
        
        case 0x05: {
        
            r->rax = (uint32_t) kopen ((const char *) (uintptr_t) (uint32_t) r->rbx, (uint32_t) r->rcx);
            break;
        
        }
        
        case 0x06: {
        
            r->rax = (uint32_t) kclose ((uint32_t) r->rbx);
            break;
        
        }
        
        case 0x07: {                    /* waitpid */
        
            r->rax = (uint32_t) process_waitpid ((int32_t) r->rbx, (uint32_t) r->rcx, (int32_t) r->rdx, r);
            break;
        
        }
        
        case 0x0A: {                    /* unlink */
        
            r->rax = (uint32_t) kremove ((uint32_t) r->rbx, 0);
            break;
        
        }
        
        case 0x0B: {
        
            r->rax = (uint32_t) process_execve ((const char *) (uintptr_t) (uint32_t) r->rbx,
                (char *const *) (uintptr_t) (uint32_t) r->rcx, (char *const *) (uintptr_t) (uint32_t) r->rdx, r);
            break;
        
        }
        
        case 0x0C: {                    /* chdir */
        
            r->rax = (uint32_t) kchdir ((uint32_t) r->rbx);
            break;
        
        }
        
        case 0x0D: {                    /* time */
        
            r->rax = (uint32_t) syscall_time ((uint32_t) r->rbx, 1);
            break;
        
        }
        
        case 0x13: {                    /* lseek */
        
            r->rax = (uint32_t) kseek ((uint32_t) r->rbx, (int32_t) r->rcx, (int32_t) r->rdx);
            break;
        
        }
        
        case 0x14: {
        
            r->rax = thread_get_current ()->owner->pid;
            break;
        
        }
        
        case 0x1E: {                    /* utime */
        
            r->rax = (uint32_t) kutime ((uint32_t) r->rbx, (uint32_t) r->rcx, 1);
            break;
        
        }
        
        case 0x21: {                    /* access */
        
            r->rax = (uint32_t) kaccess ((uint32_t) r->rbx, (int32_t) r->rcx);
            break;
        
        }
        
        case 0x26: {                    /* rename */
        
            r->rax = (uint32_t) krename ((uint32_t) r->rbx, (uint32_t) r->rcx);
            break;
        
        }
        
        case 0x27: {                    /* mkdir */
        
            r->rax = (uint32_t) kmkdir ((uint32_t) r->rbx, (mode_t) r->rcx);
            break;
        
        }
        
        case 0x28: {                    /* rmdir */
        
            r->rax = (uint32_t) kremove ((uint32_t) r->rbx, 1);
            break;
        
        }
        
        case 0x29: {                    /* dup */
        
            r->rax = (uint32_t) kdup ((int) r->rbx);
            break;
        
        }
        
        case 0x2A: {                    /* pipe */
        
            r->rax = (uint32_t) kpipe ((uint32_t) r->rbx);
            break;
        
        }
        
        case 0x36: {
        
            r->rax = (uint32_t) kioctl ((uint32_t) r->rbx, (uint32_t) r->rcx, (void *) (uintptr_t) (uint32_t) r->rdx);
            break;
        
        }
        
        case 0x3F: {                    /* dup2 */
        
            r->rax = (uint32_t) kdup2 ((int) r->rbx, (int) r->rcx);
            break;
        
        }
        
        case 0x5A: {
        
            struct mmap_args32 args;
            
            if (!mmu_copy_user (thread_get_current ()->owner->pd, (uint32_t) r->rbx, &args, sizeof (args), 0)) {
            
                r->rax = (uint32_t) -EFAULT;
                break;
            
            }
            
            r->rax = (uint32_t) (uintptr_t) mmap (
                (void *) (uintptr_t) args.addr,
                (size_t) args.length,
                (int) args.prot,
                (int) (args.flags | (args.addr ? 0 : 0x40)),
                (int) args.fd,
                (uintptr_t) args.offset
            );
            
            break;
        
        }
        
        case 0x5B: {
        
            r->rax = (uint32_t) munmap ((void *) (uintptr_t) (uint32_t) r->rbx, (uint32_t) r->rcx);
            break;
        
        }
        
        case 0x5D: {                    /* ftruncate */
        
            r->rax = (uint32_t) kftruncate ((int32_t) r->rbx, (int32_t) r->rcx);
            break;
        
        }
        
        case 0x63: {                    /* statfs */
        
            r->rax = (uint32_t) kstatfs ((uint32_t) r->rbx, (uint32_t) r->rcx, 1);
            break;
        
        }
        
        case 0x64: {                    /* fstatfs */
        
            r->rax = (uint32_t) kfstatfs ((int32_t) r->rbx, (uint32_t) r->rcx, 1);
            break;
        
        }
        
        case 0x6A /* stat */:   case 0x6B /* lstat */: {
        
            r->rax = (uint32_t) kstat ((uint32_t) r->rbx, (uint32_t) r->rcx);
            break;
        
        }
        
        case 0x6C: {                    /* fstat */
        
            r->rax = (uint32_t) kfstat ((int32_t) r->rbx, (uint32_t) r->rcx);
            break;
        
        }
        
        case 0x72: {                    /* wait4 */
        
            r->rax = (uint32_t) (r->rsi ? -EINVAL : process_waitpid ((int32_t) r->rbx, (uint32_t) r->rcx, (int32_t) r->rdx, r));
            break;
        
        }
        
        case 0x8E: {                    /* select */
        
            r->rax = (uint32_t) kselect ((int32_t) r->rbx, (uint32_t) r->rcx, (uint32_t) r->rdx, (uint32_t) r->rsi, (uint32_t) r->rdi, 1);
            
            if ((int32_t) r->rax == -EAGAIN && thread_get_current ()->state == TS_SELECT) {
            
                r->rax = 0;
                schedule (r);
            
            }
            
            break;
        
        }
        
        case 0xA2: {
        
            struct timespec32 requested32;
            struct timespec requested;
            
            uint64_t delay = 0;
            
            if (!mmu_copy_user (thread_get_current ()->owner->pd, (uint32_t) r->rbx, &requested32, sizeof (requested32), 0)) {
            
                r->rax = (uint32_t) -EFAULT;
                break;
            
            }
            
            requested.tv_sec = requested32.tv_sec;
            requested.tv_nsec = requested32.tv_nsec;
            
            r->rax = (uint32_t) syscall_nanosleep (&requested, 0, &delay);
            
            if (!r->rax && delay) {
                thread_sleep_from_syscall (delay, r);
            }
            
            break;
        
        }
        
        case 0xB7: {                    /* getcwd */
        
            r->rax = (uint32_t) kgetcwd ((uint32_t) r->rbx, (uint32_t) r->rcx);
            break;
        
        }
        
        case 0xDC: {                    /* getdents64 */
        
            r->rax = (uint32_t) kgetdents64 ((int32_t) r->rbx, (uint32_t) r->rcx, (uint32_t) r->rdx);
            break;
        
        }
        
        case 0x109: {                   /* clock_gettime */
        
            r->rax = (uint32_t) syscall_clock_gettime ((int32_t) r->rbx, (uint32_t) r->rcx, 1);
            break;
        
        }
        
        default:
        
            r->rax = (uint32_t) -ENOSYS;
            return 1;
    
    }
    
    return 0;

}

int handle_syscall (struct regs *r) {

    if (r->cs == 0x1B) {
        return handle_syscall_compat32 (r);
    }
    
    switch (r->rax) {
    
        case 0x00: {
        
            r->rax = kread (r->rdi, r->rsi, r->rdx);
            
            if (r->rax == (uintptr_t) -EAGAIN && (thread_get_current ()->state == TS_WAITIO || thread_get_current ()->state == TS_PIPE)) {
            
                r->rax = 0;
                schedule (r);
            
            }
            
            break;
        
        }
        
        case 0x01: {
        
            r->rax = kwrite (r->rdi, r->rsi, r->rdx);
            
            if (thread_get_current ()->state == TS_PIPE || thread_get_current ()->pending_signal == SIGPIPE) {
                schedule (r);
            }
            
            break;
        
        }
        
        case 0x02: {
        
            r->rax = kopen (r->rdi, r->rsi);
            break;
        
        }
        
        case 0x03: {
        
            r->rax = kclose (r->rdi);
            break;
        
        }
        
        case 0x04 /* stat */:   case 0x06 /* lstat */: {
        
            r->rax = (uintptr_t) kstat (r->rdi, r->rsi);
            break;
        
        }
        
        case 0x05: {                    /* fstat */
        
            r->rax = (uintptr_t) kfstat ((int) r->rdi, r->rsi);
            break;
        
        }
        
        case 0x08: {
        
            r->rax = kseek (r->rdi, r->rsi, r->rdx);
            break;
        
        }
        
        case 0x09: {
        
            r->rax = (uintptr_t) mmap (
                (void *) r->rdi,
                (size_t) r->rsi,
                (int) r->rdx,
                (int) r->r10,
                (int) r->r8,
                (int) r->r9
            );
            
            break;
        
        }
        
        case 0x10: {
        
            r->rax = kioctl (r->rdi, r->rsi, (void *) r->rdx);
            break;
        
        }
        
        case 0x0B: {
        
            r->rax = munmap (r->rdi, r->rsi);
            break;
        
        }
        
        case 0x15: {                    /* access */
        
            r->rax = (uintptr_t) kaccess (r->rdi, (int) r->rsi);
            break;
        
        }
        
        case 0x16: {                    /* pipe */
        
            r->rax = (uintptr_t) kpipe (r->rdi);
            break;
        
        }
        
        case 0x17: {                    /* select */
        
            r->rax = (uintptr_t) kselect ((int) r->rdi, r->rsi, r->rdx, r->r10, r->r8, 0);
            
            if ((intptr_t) r->rax == -EAGAIN && thread_get_current ()->state == TS_SELECT) {
            
                r->rax = 0;
                schedule (r);
            
            }
            
            break;
        
        }
        
        case 0x20: {                    /* dup */
        
            r->rax = (uintptr_t) kdup ((int) r->rdi);
            break;
        
        }
        
        case 0x21: {                    /* dup2 */
        
            r->rax = (uintptr_t) kdup2 ((int) r->rdi, (int) r->rsi);
            break;
        
        }
        
        case 0x23: {

            uint64_t delay = 0;
            r->rax = syscall_nanosleep ((const struct timespec *) r->rdi, (struct timespec *) r->rsi, &delay);
            
            if (!r->rax && delay) {
                thread_sleep_from_syscall (delay, r);
            }
            
            break;
        
        }
        
        case 0x27: {
        
            r->rax = thread_get_current ()->owner->pid;
            break;
        
        }
        
        case 0x39: {
        
            uintptr_t pd = mmu_clone_address_space (thread_get_current ()->owner->pd);
            
            if (!pd) {
                r->rax = (uintptr_t) -ENOMEM;
            } else {
            
                r->rax = process_fork_from_clone (r, pd);
                
                if ((intptr_t) r->rax < 0) {
                    mmu_destroy_address_space (pd);
                }
            
            }
            
            break;
        
        }
        
        case 0x3B: {
        
            r->rax = process_execve ((const char *) r->rdi, (char *const *) r->rsi, (char *const *) r->rdx, r);
            break;
        
        }
        
        case 0x3C /* exit */:   case 0xE7 /* exit_group */: {
        
            process_exit ((int) r->rdi, r);
            break;
        
        }
        
        case 0x3D: {                    /* wait4 */
        
            r->rax = r->r10 ? (uintptr_t) -EINVAL : (uintptr_t) process_waitpid ((int) r->rdi, r->rsi, (int) r->rdx, r);
            break;
        
        }
        
        case 0x4D: {                    /* ftruncate */
        
            if ((int64_t) r->rsi < 0) {
                r->rax = (uintptr_t) -EINVAL;
            } else if (r->rsi > 0x7FFFFFFF) {
                r->rax = (uintptr_t) -EFBIG;
            } else {
                r->rax = (uintptr_t) kftruncate ((int) r->rdi, (off_t) r->rsi);
            }
            
            break;
        
        }
        
        case 0x4F: {                    /* getcwd */
        
            r->rax = (uintptr_t) kgetcwd (r->rdi, (size_t) r->rsi);
            break;
        
        }
        
        case 0x50: {                    /* chdir */
        
            r->rax = (uintptr_t) kchdir (r->rdi);
            break;
        
        }
        
        case 0x52: {                    /* rename */
        
            r->rax = (uintptr_t) krename (r->rdi, r->rsi);
            break;
        
        }
        
        case 0x53: {                    /* mkdir */
        
            r->rax = (uintptr_t) kmkdir (r->rdi, (mode_t) r->rsi);
            break;
        
        }
        
        case 0x54: {                    /* rmdir */
        
            r->rax = (uintptr_t) kremove (r->rdi, 1);
            break;
        
        }
        
        case 0x57: {                    /* unlink */
        
            r->rax = (uintptr_t) kremove (r->rdi, 0);
            break;
        
        }
        
        case 0x84: {                    /* utime */
        
            r->rax = (uintptr_t) kutime (r->rdi, r->rsi, 0);
            break;
        
        }
        
        case 0x89: {                    /* statfs */
        
            r->rax = (uintptr_t) kstatfs (r->rdi, r->rsi, 0);
            break;
        
        }
        
        case 0x8A: {                    /* fstatfs */
        
            r->rax = (uintptr_t) kfstatfs ((int) r->rdi, r->rsi, 0);
            break;
        
        }
        
        case 0xC9: {                    /* time */
        
            r->rax = (uintptr_t) syscall_time (r->rdi, 0);
            break;
        
        }
        
        case 0xD9: {                    /* getdents64 */
        
            r->rax = (uintptr_t) kgetdents64 ((int) r->rdi, r->rsi, (size_t) r->rdx);
            break;
        
        }
        
        case 0xE4: {                    /* clock_gettime */
        
            r->rax = (uintptr_t) syscall_clock_gettime ((int) r->rdi, r->rsi, 0);
            break;
        
        }
        
        case 0x1000: {                  /* ipc_send */
        
            r->rax = ipc_send ((uint32_t) r->rdi, (const ipc_message_t *) r->rsi, r);
            break;
        
        }
        
        case 0x1001: {                  /* ipc_recieve */
        
            r->rax = ipc_receive ((uint32_t) r->rdi, (ipc_message_t *) r->rsi, r);
            break;
        
        }
        
        case 0x1002: {                  /* ipc_send_recieve */
        
            r->rax = ipc_sendrec ((uint32_t) r->rdi, (ipc_message_t *) r->rsi, r);
            break;
        
        }
        
        default:
        
            r->rax = (uintptr_t) -ENOSYS;
            return 1;
    
    }
    
    return 0;

}
#else
__attribute__ ((packed)) struct mmap_args {

    uint32_t addr;
    uint32_t length;
    uint32_t prot;
    uint32_t flags;
    uint32_t fd;
    uint32_t offset;

};

static int handle_syscall (struct regs *r) {

    switch (r->eax) {
    
        case 0x01 /* exit */:   case 0xFC /* exit_group */: {
        
            process_exit ((int) r->ebx, r);
            break;
        
        }
        
        case 0x02: {
        
            uintptr_t pd = mmu_clone_address_space (thread_get_current ()->owner->pd);
            
            if (!pd) {
                r->eax = (uintptr_t) -ENOMEM;
            } else {
            
                r->eax = process_fork_from_clone (r, pd);
                
                if ((intptr_t) r->eax < 0) {
                    mmu_destroy_address_space (pd);
                }
            
            }
            
            break;
        
        }
        
        case 0x03: {
        
            r->eax = kread (r->ebx, r->ecx, r->edx);
            
            if (r->eax == (uintptr_t) -EAGAIN && (thread_get_current ()->state == TS_WAITIO || thread_get_current ()->state == TS_PIPE)) {
            
                r->eax = 0;
                schedule (r);
            
            }
            
            break;
        
        }
        
        case 0x04: {
        
            r->eax = kwrite (r->ebx, r->ecx, r->edx);
            
            if (thread_get_current ()->state == TS_PIPE || thread_get_current ()->pending_signal == SIGPIPE) {
                schedule (r);
            }
            
            break;
        
        }
        
        case 0x05: {
        
            r->eax = kopen ((const char *) r->ebx, r->ecx);
            break;
        
        }
        
        case 0x06: {
        
            r->eax = kclose (r->ebx);
            break;
        
        }
        
        case 0x07: {                    /* waitpid */
        
            r->eax = (uintptr_t) process_waitpid ((int) r->ebx, r->ecx, (int) r->edx, r);
            break;
        
        }
        
        case 0x0A: {                    /* unlink */
        
            r->eax = (uintptr_t) kremove (r->ebx, 0);
            break;
        
        }
        
        case 0x0B: {
        
            r->eax = process_execve ((const char *) r->ebx, (char *const *) r->ecx, (char *const *) r->edx, r);
            break;
        
        }
        
        case 0x0C: {                    /* chdir */
        
            r->eax = (uintptr_t) kchdir (r->ebx);
            break;
        
        }
        
        case 0x0D: {                    /* time */
        
            r->eax = (uintptr_t) syscall_time (r->ebx, 1);
            break;
        
        }
        
        case 0x13: {
        
            r->eax = kseek (r->ebx, r->ecx, r->edx);
            break;
        
        }
        
        case 0x14: {
        
            r->eax = thread_get_current ()->owner->pid;
            break;
        
        }
        
        case 0x1E: {                    /* utime */
        
            r->eax = (uintptr_t) kutime (r->ebx, r->ecx, 1);
            break;
        
        }
        
        case 0x21: {                    /* access */
        
            r->eax = (uintptr_t) kaccess (r->ebx, (int) r->ecx);
            break;
        
        }
        
        case 0x26: {                    /* rename */
        
            r->eax = (uintptr_t) krename (r->ebx, r->ecx);
            break;
        
        }
        
        case 0x27: {                    /* mkdir */
        
            r->eax = (uintptr_t) kmkdir (r->ebx, (mode_t) r->ecx);
            break;
        
        }
        
        case 0x28: {                    /* rmdir */
        
            r->eax = (uintptr_t) kremove (r->ebx, 1);
            break;
        
        }
        
        case 0x29: {                    /* dup */
        
            r->eax = (uintptr_t) kdup ((int) r->ebx);
            break;
        
        }
        
        case 0x2A: {                    /* pipe */
        
            r->eax = kpipe (r->ebx);
            break;
        
        }
        
        case 0x36: {
        
            r->eax = kioctl (r->ebx, r->ecx, (void *) r->edx);
            break;
        
        }
        
        case 0x3F: {                    /* dup2 */
        
            r->eax = (uintptr_t) kdup2 ((int) r->ebx, (int) r->ecx);
            break;
        
        }
        
        case 0x5A: {
        
            struct mmap_args *args = (struct mmap_args *) r->ebx;
            
            r->eax = (uintptr_t) mmap (
                (void *) args->addr,
                (size_t) args->length,
                (int) args->prot,
                (int) args->flags,
                (int) args->fd,
                (uint32_t) args->offset
            );
            
            break;
        
        }
        
        case 0x5B: {
        
            r->eax = munmap (r->ebx, r->ecx);
            break;
        
        }
        
        case 0x5D: {                    /* ftruncate */
        
            r->eax = (uintptr_t) kftruncate ((int) r->ebx, (off_t) r->ecx);
            break;
        
        }
        
        case 0x63: {                    /* statfs */
        
            r->eax = (uintptr_t) kstatfs (r->ebx, r->ecx, 1);
            break;
        
        }
        
        case 0x64: {                    /* fstatfs */
        
            r->eax = (uintptr_t) kfstatfs ((int) r->ebx, r->ecx, 1);
            break;
        
        }
        
        case 0x6A /* stat */:   case 0x6B /* lstat */: {
        
            r->eax = (uintptr_t) kstat (r->ebx, r->ecx);
            break;
        
        }
        
        case 0x6C: {                    /* fstat */
        
            r->eax = (uintptr_t) kfstat ((int) r->ebx, r->ecx);
            break;
        
        }
        
        case 0x72: {                    /* wait4 */
        
            r->eax = r->esi ? (uintptr_t) -EINVAL : (uintptr_t) process_waitpid ((int) r->ebx, r->ecx, (int) r->edx, r);
            break;
        
        }
        
        case 0x8E: {                    /* select */
        
            r->eax = (uintptr_t) kselect ((int) r->ebx, r->ecx, r->edx, r->esi, r->edi, 1);
            
            if (r->eax == (uintptr_t) -EAGAIN && thread_get_current ()->state == TS_SELECT) {
            
                r->eax = 0;
                schedule (r);
            
            }
            
            break;
        
        }
        
        case 0xA2: {

            uint64_t delay = 0;
            r->eax = syscall_nanosleep ((const struct timespec *) r->ebx, (struct timespec *) r->ecx, &delay);
            
            if (!r->eax && delay) {
                thread_sleep_from_syscall (delay, r);
            }
            
            break;
        
        }
        
        case 0xB7: {                    /* getcwd */
        
            r->eax = (uintptr_t) kgetcwd (r->ebx, (size_t) r->ecx);
            break;
        
        }
        
        case 0xDC: {                    /* getdents64 */
        
            r->eax = (uintptr_t) kgetdents64 ((int) r->ebx, r->ecx, r->edx);
            break;
        
        }
        
        case 0x109: {                   /* clock_gettime */
        
            r->eax = (uintptr_t) syscall_clock_gettime ((int) r->ebx, r->ecx, 0);
            break;
        
        }
        
        case 0x1000: {                  /* ipc_send */
        
            r->eax = ipc_send ((uint32_t) r->ebx, (const ipc_message_t *) r->ecx, r);
            break;
        
        }
        
        case 0x1001: {                  /* ipc_recieve */
        
            r->eax = ipc_receive ((uint32_t) r->ebx, (ipc_message_t *) r->ecx, r);
            break;
        
        }
        
        case 0x1002: {                  /* ipc_send_recieve */
        
            r->eax = ipc_sendrec ((uint32_t) r->ebx, (ipc_message_t *) r->ecx, r);
            break;
        
        }
        
        default:
        
            r->eax = (uintptr_t) -ENOSYS;
            return 1;
    
    }
    
    return 0;

}
#endif

void syscalls_register (void) {
    isr_install_handler (0x80,  &handle_syscall, "syscalls");
}

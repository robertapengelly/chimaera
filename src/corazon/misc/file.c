/******************************************************************************
 * @file            file.c
 *****************************************************************************/
#include    <corazon/tasking/process.h>
#include    <corazon/utils/misc.h>
#include    <corazon/vfs/node.h>
#include    <corazon/vfs/vfs.h>
#include    "corazon/timer.h"

#include    <sys/select.h>
#include    <sys/stat.h>
#include    <sys/types.h>

#include    <dirent.h>
#include    <errno.h>
#include    <stdio.h>
#include    <string.h>
#include    <unistd.h>

extern int cmos_get_unix_time (uint64_t *seconds);

__attribute__ ((packed)) struct linux_dirent64 {

    uint64_t d_ino;
    int64_t d_off;
    
    uint16_t d_reclen;
    uint8_t d_type;
    
    char d_name[264];

};

ssize_t kgetdents64 (int fd, uintptr_t address, size_t count) {

    struct process *process = thread_get_current ()->owner;
    struct linux_dirent64 entry64;
    
    struct dirent entry;
    struct fs_node *node;
    
    uint32_t index;
    size_t written = 0, length, size;
    
    int ret;
    
    if (fd < 0 || fd >= 256) {
        return -EBADF;
    }
    
    node = process_get_file (process, fd);
    
    if (!node || node == (struct fs_node *) -1) {
        return -EBADF;
    }
    
    if (!node->readdir) {
        return -ENOTDIR;
    }
    
    if (!address) {
        return -EFAULT;
    }
    
    if (count < 24) {
        return -EINVAL;
    }
    
    index = process->dir_index[fd];
    
    while (count - written >= 24) {
    
        memset (&entry, 0, sizeof (entry));
        ret = vfs_readdir (node, index, &entry);
        
        if (ret <= 0) {
            return written ? (ssize_t) written : (ssize_t) (ret < 0 ? ret : 0);
        }
        
        entry.d_name[255] = '\0';
        length = strlen (entry.d_name) + 1;
        
        size = (19 + length + 7) & ~(size_t) 7;
        
        if (size > count - written) {
            return written ? (ssize_t) written : -EINVAL;
        }
        
        if (entry.d_off < 0 || (uint32_t) entry.d_off <= index) {
            return written ? (ssize_t) written : -EIO;
        }
        
        memset (&entry64, 0, size);
        
        entry64.d_ino = entry.d_ino;
        entry64.d_off = entry.d_off;
        entry64.d_reclen = (uint16_t) size;
        entry64.d_type = entry.d_type;
        
        memcpy (entry64.d_name, entry.d_name, length);
        
        if (!mmu_copy_user (process->pd, address + written, &entry64, size, 1)) {
            return written ? (ssize_t) written : -EFAULT;
        }
        
        index = (uint32_t) entry.d_off;
        process->dir_index[fd] = index;
        
        written += size;
    
    }
    
    return (ssize_t) written;

}

int kclose (int __fd) {
    return process_close_file (thread_get_current ()->owner, __fd);
}

int kopen (const char *__file, int __oflag) {

    struct fs_node *node = 0;
    int ret, error;
    
    if (!(node = vfs_open_flags (__file, __oflag, &error))) {
        return error;
    }
    
    if ((ret = process_add_file (thread_get_current ()->owner, node)) < 0) {
    
        vfs_close (node);
        return ret;
    
    }
    
    return ret;

}

int kdup (int __fd) {

    struct process *process = thread_get_current ()->owner;
    
    struct fs_node *node;
    int ret;
    
    node = process_get_file (process, __fd);
    
    if (!node || node == (struct fs_node *) -1) {
        return -EBADF;
    }
    
    if ((ret = process_add_file (process, node)) < 0) {
        return ret;
    }
    
    vfs_retain (node);
    
    process->dir_index[ret] = process->dir_index[__fd];
    return ret;

}

int kdup2 (int __fd, int __newfd) {

    struct process *process = thread_get_current ()->owner;
    struct fs_node *node;
    
    if (__newfd < 0 || __newfd >= 256) {
        return -EBADF;
    }
    
    node = process_get_file (process, __fd);
    
    if (!node || node == (struct fs_node *) -1) {
        return -EBADF;
    }
    
    if (__fd == __newfd) {
        return __newfd;
    }
    
    vfs_retain (node);
    
    if (process->fd[__newfd]) {
        process_close_file (process, __newfd);
    }
    
    process->fd[__newfd] = node;
    process->dir_index[__newfd] = process->dir_index[__fd];
    
    return __newfd;

}

int kioctl (int __fd, unsigned long request, void *arg) {

    struct fs_node *node;
    
    if (__fd < 0 || __fd >= 256) {
        return -EBADF;
    }
    
    node = process_get_file (thread_get_current ()->owner, __fd);
    
    if (!node || node == (struct fs_node *) -1) {
        return -EBADF;
    }
    
    if (!node->kioctl) {
        return -ENOTTY;
    }
    
    return node->kioctl (node, request, arg);

}

off_t kseek (int __fd, off_t __offset, int __whence) {

    struct process *process = thread_get_current ()->owner;
    struct fs_node *node = process_get_file (process, __fd);
    
    if (!node || node == (struct fs_node *) -1) {
        return -EBADF;
    }
    
    if (node->flags == INO_DIR && __offset == 0 && __whence == SEEK_SET) {
    
        process->dir_index[__fd] = 0;
        return 0;
    
    }
    
    return vfs_seek (node, __offset, __whence);

}

ssize_t kread (int __fd, void *__buf, size_t __nbytes) {

    struct fs_node *node = process_get_file (thread_get_current ()->owner, __fd);
    
    if (!node || node == (struct fs_node *) -1) {
        return -EBADF;
    }
    
    return vfs_read (node, __buf, __nbytes);

}

ssize_t kwrite (int __fd, void *__buf, size_t __nbytes) {

    struct fs_node *node = process_get_file (thread_get_current ()->owner, __fd);
    
    if (!node || node == (struct fs_node *) -1) {
        return -EBADF;
    }
    
    return vfs_write (node, __buf, __nbytes);

}

int kftruncate (int __fd, off_t __length) {

    struct fs_node *node = process_get_file (thread_get_current ()->owner, __fd);
    
    if (!node || node == (struct fs_node *) -1) {
        return -EBADF;
    }
    
    return vfs_truncate (node, __length);

}

int kstat (uintptr_t path_address, uintptr_t stat_address) {

    struct process *process = thread_get_current ()->owner;
    
    struct fs_node *node;
    struct stat st;
    
    char path[4096];
    size_t i;
    
    int ret;
    
    if (!path_address || !stat_address) {
        return -EFAULT;
    }
    
    for (i = 0; i < sizeof (path); i++) {
    
        if (path_address > (uintptr_t) -1 - i || !mmu_copy_user (process->pd, path_address + i, path + i, 1, 0)) {
            return -EFAULT;
        }
        
        if (!path[i]) {
            break;
        }
    
    }
    
    if (i == sizeof (path)) {
        return -ENAMETOOLONG;
    }
    
    if (!(node = vfs_open_for_stat (path, &ret))) {
        return ret;
    }
    
    ret = vfs_stat (node, &st);
    vfs_close (node);
    
    if (ret < 0) {
        return ret;
    }
    
    if (!mmu_copy_user (process->pd, stat_address, &st, sizeof (st), 1)) {
        return -EFAULT;
    }
    
    return 0;

}

int kfstat (int fd, uintptr_t stat_address) {

    struct process *process = thread_get_current ()->owner;
    
    struct fs_node *node;
    struct stat st;
    
    int ret;
    
    if (fd < 0 || fd >= 256 || !(node = process_get_file (process, fd)) || node == (struct fs_node *) -1) {
        return -EBADF;
    }
    
    if (!stat_address) {
        return -EFAULT;
    }
    
    ret = vfs_stat (node, &st);
    
    if (ret < 0) {
        return ret;
    }
    
    if (!mmu_copy_user (process->pd, stat_address, &st, sizeof (st), 1)) {
        return -EFAULT;
    }
    
    return 0;

}

__attribute__ ((packed)) struct linux_statfs32 {
    
    uint32_t f_type, f_bsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int32_t f_fsid[2];
    uint32_t f_namelen, f_frsize, f_flags, f_spare[4];

};

#if     defined (__AMD64__)
__attribute__ ((packed)) struct linux_statfs64 {
    
    uint64_t f_type, f_bsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int32_t f_fsid[2];
    uint64_t f_namelen, f_frsize, f_flags, f_spare[4];

};
#endif

__attribute__ ((packed)) struct timeval32 {    
    int32_t tv_sec, tv_usec;
};

static int statfs_copy_user (struct process *process, uintptr_t address, const struct vfs_statfs *fs, int compat32) {
    
    struct linux_statfs32 st32;
    
#if     defined (__AMD64__)
    struct linux_statfs64 st64;
    
    if (!compat32) {
    
        memset (&st64, 0, sizeof (st64));
        
        st64.f_type = fs->type;
        st64.f_bsize = fs->bsize;
        st64.f_blocks = fs->blocks;
        st64.f_bfree = fs->bfree;
        st64.f_bavail = fs->bavail;
        st64.f_files = fs->files;
        st64.f_ffree = fs->ffree;
        st64.f_namelen = fs->namelen;
        st64.f_frsize = fs->frsize;
        st64.f_flags = fs->flags;
        
        return mmu_copy_user (process->pd, address, &st64, sizeof (st64), 1) ? 0 : -EFAULT;
    
    }
#else
    (void) compat32;
#endif
    
    if (fs->type > UINT32_MAX || fs->bsize > UINT32_MAX || fs->blocks > UINT32_MAX ||
        fs->bfree > UINT32_MAX || fs->bavail > UINT32_MAX || fs->files > UINT32_MAX ||
        fs->ffree > UINT32_MAX || fs->namelen > UINT32_MAX || fs->frsize > UINT32_MAX || fs->flags > UINT32_MAX) {
        return -EOVERFLOW;
    }
    
    memset (&st32, 0, sizeof (st32));
    
    st32.f_type = (uint32_t) fs->type;
    st32.f_bsize = (uint32_t) fs->bsize;
    st32.f_blocks = (uint32_t) fs->blocks;
    st32.f_bfree = (uint32_t) fs->bfree;
    st32.f_bavail = (uint32_t) fs->bavail;
    st32.f_files = (uint32_t) fs->files;
    st32.f_ffree = (uint32_t) fs->ffree;
    st32.f_namelen = (uint32_t) fs->namelen;
    st32.f_frsize = (uint32_t) fs->frsize;
    st32.f_flags = (uint32_t) fs->flags;
    
    return mmu_copy_user (process->pd, address, &st32, sizeof (st32), 1) ? 0 : -EFAULT;

}

int kstatfs (uintptr_t path_address, uintptr_t stat_address, int compat32) {
    
    struct process *process = thread_get_current ()->owner;
    
    struct vfs_statfs fs;
    struct fs_node *node;
    
    char path[4096];
    size_t i;
    
    int ret;
    
    if (!path_address || !stat_address) {
        return -EFAULT;
    }
    
    for (i = 0; i < sizeof (path); i++) {
    
        if (path_address > (uintptr_t) -1 - i || !mmu_copy_user (process->pd, path_address + i, path + i, 1, 0)) {
            return -EFAULT;
        }
        
        if (!path[i]) {
            break;
        }
    
    }
    
    if (i == sizeof (path)) {
        return -ENAMETOOLONG;
    }
    
    if (!(node = vfs_open_for_stat (path, &ret))) {
        return ret;
    }
    
    ret = vfs_statfs (node, &fs);
    vfs_close (node);
    
    if (ret < 0) {
        return ret;
    }
    
    return statfs_copy_user (process, stat_address, &fs, compat32);

}

int kfstatfs (int fd, uintptr_t stat_address, int compat32) {
    
    struct process *process = thread_get_current ()->owner;
    
    struct vfs_statfs fs;
    struct fs_node *node;
    
    int ret;
    
    if (fd < 0 || fd >= 256 || !(node = process_get_file (process, fd)) || node == (struct fs_node *) -1) {
        return -EBADF;
    }
    
    if (!stat_address) {
        return -EFAULT;
    }
    
    ret = vfs_statfs (node, &fs);
    
    if (ret < 0) {
        return ret;
    }
    
    return statfs_copy_user (process, stat_address, &fs, compat32);

}

static int kselect_check (struct thread *thread) {
    
    struct process *process = thread->owner;
    struct fs_node *node;
    
    uint8_t readfds[128] = { 0 };
    uint8_t writefds[128] = { 0 };
    uint8_t exceptfds[128] = { 0 };
    
    size_t bytes = ((size_t) thread->select.nfds + 7) / 8;
    int fd, events, count = 0;
    
    for (fd = 0; fd < thread->select.nfds; fd++) {
    
        uint8_t bit = (uint8_t) (1U << (fd & 7));
        
        int read_requested = thread->select.read_address && (thread->select.readfds[fd >> 3] & bit);
        int write_requested = thread->select.write_address && (thread->select.writefds[fd >> 3] & bit);
        int except_requested = thread->select.except_address && (thread->select.exceptfds[fd >> 3] & bit);
        
        if (!read_requested && !write_requested && !except_requested) {
            continue;
        }
        
        if (fd >= 256 || !(node = process_get_file (process, fd)) || node == (struct fs_node *) -1) {
            return -EBADF;
        }
        
        if (node->kpoll) {
            events = node->kpoll (node);
        } else {
        
            events = 0;
            
            if (node->flags == INO_REG || node->flags == INO_DIR || node->flags == INO_BLK || node->kread) {
                events |= VFS_POLLIN;
            }
            
            if (node->flags == INO_REG || node->flags == INO_DIR || node->flags == INO_BLK || node->kwrite) {
                events |= VFS_POLLOUT;
            }
        
        }
        
        if (read_requested && (events & VFS_POLLIN)) {
        
            readfds[fd >> 3] |= bit;
            count++;
        
        }
        
        if (write_requested && (events & VFS_POLLOUT)) {
        
            writefds[fd >> 3] |= bit;
            count++;
        
        }
    
    }
    
    if (!count && (!thread->select.has_timeout || get_uptime_milliseconds () < thread->select.deadline)) {
        return -EAGAIN;
    }
    
    if (bytes && thread->select.read_address && !mmu_copy_user (process->pd, thread->select.read_address, readfds, bytes, 1)) {
        return -EFAULT;
    }
    
    if (bytes && thread->select.write_address && !mmu_copy_user (process->pd, thread->select.write_address, writefds, bytes, 1)) {
        return -EFAULT;
    }
    
    if (bytes && thread->select.except_address && !mmu_copy_user (process->pd, thread->select.except_address, exceptfds, bytes, 1)) {
        return -EFAULT;
    }
    
    return count;

}

void kselect_update (struct thread *thread) {
    
    int ret;
    
    if (thread->state != TS_SELECT) {
        return;
    }
    
    ret = kselect_check (thread);
    
    if (ret == -EAGAIN) {
        return;
    }
    
    thread->select.result = ret;
    thread->select.select_state = ret < 0 ? SS_ERROR : SS_FINISHED;
    thread->state = TS_RUN;
    
#if     defined (__AMD64__)
    thread->regs.rax = (uintptr_t) ret;
#else
    thread->regs.eax = (uintptr_t) ret;
#endif

}

int kselect (int nfds, uintptr_t read_address, uintptr_t write_address, uintptr_t except_address, uintptr_t timeout_address, int compat32) {
    
    struct thread *thread = thread_get_current ();
    struct process *process = thread->owner;
    
    struct timeval timeout;
    
    int64_t seconds, microseconds;
    uint64_t delay, now;
    
    size_t bytes;
    int ret;
    
    if (nfds < 0 || nfds > 1024) {
        return -EINVAL;
    }
    
    bytes = ((size_t) nfds + 7) / 8;
    memset (&thread->select, 0, sizeof (thread->select));
    
    thread->select.nfds = nfds;
    thread->select.read_address = read_address;
    thread->select.write_address = write_address;
    thread->select.except_address = except_address;
    
    if (bytes && read_address) {
    
        if (!mmu_user_buffer_valid (process->pd, read_address, bytes, 1) ||
            !mmu_copy_user (process->pd, read_address, thread->select.readfds, bytes, 0)) {
            return -EFAULT;
        }
    
    }
    
    if (bytes && write_address) {
    
        if (!mmu_user_buffer_valid (process->pd, write_address, bytes, 1) ||
            !mmu_copy_user (process->pd, write_address, thread->select.writefds, bytes, 0)) {
            return -EFAULT;
        }
    
    }
    
    if (bytes && except_address) {
    
        if (!mmu_user_buffer_valid (process->pd, except_address, bytes, 1) ||
            !mmu_copy_user (process->pd, except_address, thread->select.exceptfds, bytes, 0)) {
            return -EFAULT;
        }
    
    }
    
    if (timeout_address) {
    
#if     defined (__AMD64__)
        struct timeval32 timeout32;
        
        if (compat32) {
        
            if (!mmu_copy_user (process->pd, timeout_address, &timeout32, sizeof (timeout32), 0)) {
                return -EFAULT;
            }
            
            seconds = timeout32.tv_sec;
            microseconds = timeout32.tv_usec;
        
        } else
#endif
        {
        
            if (!mmu_copy_user (process->pd, timeout_address, &timeout, sizeof (timeout), 0)) {
                return -EFAULT;
            }
            
            seconds = timeout.tv_sec;
            microseconds = timeout.tv_usec;
        
        }
        
        if (seconds < 0 || microseconds < 0 || microseconds >= 1000000) {
            return -EINVAL;
        }
        
        if ((uint64_t) seconds > (UINT64_MAX - 1000ULL) / 1000ULL) {
            return -EINVAL;
        }
        
        delay = (uint64_t) seconds * 1000ULL + ((uint64_t) microseconds + 999ULL) / 1000ULL;
        now = get_uptime_milliseconds ();
        
        thread->select.has_timeout = 1;
        thread->select.deadline = delay > UINT64_MAX - now ? UINT64_MAX : now + delay;
    
    }

#if     !defined (__AMD64__)
    (void) compat32;
#endif
    
    ret = kselect_check (thread);
    
    if (ret != -EAGAIN) {
        return ret;
    }
    
    thread->select.select_state = SS_STARTED;
    thread->state = TS_SELECT;
    
    return -EAGAIN;

}

int kmkdir (uintptr_t path_address, mode_t mode) {

    struct process *process = thread_get_current ()->owner;
    
    char path[4096];
    size_t i;
    
    if (!path_address) {
        return -EFAULT;
    }
    
    for (i = 0; i < sizeof (path); i++) {
    
        if (path_address > (uintptr_t) -1 - i || !mmu_copy_user (process->pd, path_address + i, path + i, 1, 0)) {
            return -EFAULT;
        }
        
        if (!path[i]) {
            break;
        }
    
    }
    
    if (i == sizeof (path)) {
        return -ENAMETOOLONG;
    }
    
    return vfs_mkdir (path, mode);

}

int krename (uintptr_t old_address, uintptr_t new_address) {

    struct process *process = thread_get_current ()->owner;
    
    char old_path[4096], new_path[4096];
    size_t i;
    
    if (!old_address || !new_address) {
        return -EFAULT;
    }
    
    for (i = 0; i < sizeof (old_path); i++) {
    
        if (old_address > (uintptr_t) -1 - i || !mmu_copy_user (process->pd, old_address + i, old_path + i, 1, 0)) {
            return -EFAULT;
        }
        
        if (!old_path[i]) {
            break;
        }
    
    }
    
    if (i == sizeof (old_path)) {
        return -ENAMETOOLONG;
    }
    
    for (i = 0; i < sizeof (new_path); i++) {
    
        if (new_address > (uintptr_t) -1 - i || !mmu_copy_user (process->pd, new_address + i, new_path + i, 1, 0)) {
            return -EFAULT;
        }
        
        if (!new_path[i]) {
            break;
        }
    
    }
    
    if (i == sizeof (new_path)) {
        return -ENAMETOOLONG;
    }
    
    return vfs_rename (old_path, new_path);

}

int kremove (uintptr_t path_address, int directory) {

    struct process *process = thread_get_current ()->owner;
    
    char path[4096];
    size_t i;
    
    if (!path_address) {
        return -EFAULT;
    }
    
    for (i = 0; i < sizeof (path); i++) {
    
        if (path_address > (uintptr_t) -1 - i || !mmu_copy_user (process->pd, path_address + i, path + i, 1, 0)) {
            return -EFAULT;
        }
        
        if (!path[i]) {
            break;
        }
    
    }
    
    if (i == sizeof (path)) {
        return -ENAMETOOLONG;
    }
    
    return vfs_remove (path, directory);

}

int kaccess (uintptr_t path_address, int mode) {

    struct process *process = thread_get_current ()->owner;
    
    struct fs_node *node;
    struct stat st;
    
    char path[4096];
    size_t i;
    
    int ret;
    
    if (mode & ~(R_OK | W_OK | X_OK)) {
        return -EINVAL;
    }
    
    if (!path_address) {
        return -EFAULT;
    }
    
    for (i = 0; i < sizeof (path); i++) {
    
        if (path_address > (uintptr_t) -1 - i || !mmu_copy_user (process->pd, path_address + i, path + i, 1, 0)) {
            return -EFAULT;
        }
        
        if (!path[i]) {
            break;
        }
    
    }
    
    if (i == sizeof (path)) {
        return -ENAMETOOLONG;
    }
    
    if (!i) {
        return -ENOENT;
    }
    
    if (!(node = vfs_open_for_stat (path, &ret))) {
        return ret;
    }
    
    if (mode == F_OK) {
    
        vfs_close (node);
        return 0;
    
    }
    
    ret = vfs_stat (node, &st);
    vfs_close (node);
    
    if (ret < 0) {
        return ret;
    }
    
    if (((st.st_mode >> 6) & (mode_t) mode) != (mode_t) mode) {
        return -EACCES;
    }
    
    return 0;

}

int kutime (uintptr_t path_address, uintptr_t times_address, int compat32) {

    struct process *process = thread_get_current ()->owner;
    struct fs_node *node;
    
    int64_t times[2];
    uint64_t seconds;
    
    char path[4096];
    size_t i;
    
    int ret;
    
    if (!path_address) {
        return -EFAULT;
    }
    
    for (i = 0; i < sizeof (path); i++) {
    
        if (path_address > (uintptr_t) -1 - i || !mmu_copy_user (process->pd, path_address + i, path + i, 1, 0)) {
            return -EFAULT;
        }
        
        if (!path[i]) {
            break;
        }
    
    }
    
    if (i == sizeof (path)) {
        return -ENAMETOOLONG;
    }
    
    if (!i) {
        return -ENOENT;
    }
    
    if (!times_address) {
    
        if (!cmos_get_unix_time (&seconds)) {
            return -EIO;
        }
        
        times[0] = (int64_t) seconds;
        times[1] = (int64_t) seconds;
    
    } else if (compat32) {
    
        int32_t times32[2];
        
        if (!mmu_copy_user (process->pd, times_address, times32, sizeof (times32), 0)) {
            return -EFAULT;
        }
        
        times[0] = (int64_t) times32[0];
        times[1] = (int64_t) times32[1];
    
    } else {
    
        if (!mmu_copy_user (process->pd, times_address, times, sizeof (times), 0)) {
            return -EFAULT;
        }
    
    }
    
    if (!(node = vfs_open_for_stat (path, &ret))) {
        return ret;
    }
    
    if (node->kutime) {
        ret = node->kutime (node, (int64_t) times[0], (int64_t) times[1]);
    } else {
        ret = -EROFS;
    }
    
    vfs_close (node);
    return ret;

}

int kchdir (uintptr_t path_address) {

    struct process *process = thread_get_current ()->owner;
    struct fs_node *node;
    
    char *absolute, path[PROCESS_CWD_MAX];
    size_t i, length;
    
    int error;
    
    if (!path_address) {
        return -EFAULT;
    }
    
    for (i = 0; i < sizeof (path); i++) {
    
        if (path_address > (uintptr_t) -1 - i || !mmu_copy_user (process->pd, path_address + i, path + i, 1, 0)) {
            return -EFAULT;
        }
        
        if (!path[i]) {
            break;
        }
    
    }
    
    if (i == sizeof (path)) {
        return -ENAMETOOLONG;
    }
    
    if (!i) {
        return -ENOENT;
    }
    
    if (!(absolute = vfs_canonicalize_path (path, &error))) {
        return error;
    }
    
    length = strlen (absolute);
    
    if (length >= sizeof (process->cwd)) {
    
        kfree (absolute);
        return -ENAMETOOLONG;
    
    }
    
    if (!(node = vfs_open_flags (absolute, VFS_O_RDONLY, &error))) {
    
        kfree (absolute);
        return error;
    
    }
    
    if (node->flags != INO_DIR) {
    
        vfs_close (node);
        
        kfree (absolute);
        return -ENOTDIR;
    
    }
    
    vfs_close (node);
    
    memcpy (process->cwd, absolute, length + 1);
    kfree (absolute);
    
    return 0;

}

intptr_t kgetcwd (uintptr_t address, size_t size) {

    struct process *process = thread_get_current ()->owner;
    size_t length = strlen (process->cwd) + 1;
    
    if (!address) {
        return -EFAULT;
    }
    
    if (size < length) {
        return -ERANGE;
    }
    
    if (!mmu_copy_user (process->pd, address, process->cwd, length, 1)) {
        return -EFAULT;
    }
    
    return (intptr_t) length;

}

/******************************************************************************
 * @file            proc.c
 *****************************************************************************/
#include    <corazon/tasking/process.h>
#include    <corazon/utils/misc.h>
#include    <corazon/vfs/node.h>
#include    <corazon/vfs/vfs.h>
#include    <corazon/mmu.h>
#include    <corazon/timer.h>

#include    <errno.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#define     PROC_UPTIME                 1
#define     PROC_VERSION                2
#define     PROC_MEMINFO                3

static int (*procfs_mapper_readdir) (void *data, uint32_t index, struct dirent *de) = 0;

struct procfs_file {

    size_t offset, length;
    char text[256];

};

static const char *procfs_state_name (enum thread_state state) {

    switch (state) {
    
        case TS_RUN:
        
            return "RUN";
        
        case TS_WAITIO:
        
            return "WAITIO";
        
        case TS_WAITCHILD:
        
            return "WAITCHILD";
        
        case TS_SLEEP:
        
            return "SLEEP";
        
        case TS_SELECT:
        
            return "SELECT";
        
        case TS_PIPE:
        
            return "PIPE";
        
        case TS_SUSPEND:
        
            return "SUSPEND";
        
        case TS_CRITICAL:
        
            return "CRITICAL";
        
        case TS_UNINTERRUPTIBLE:
        
            return "UNINTERRUPTIBLE";
        
        case TS_DEAD:
        
            return "DEAD";
    
    }
    
    return "UNKNOWN";

}

static int procfs_lookup (uint32_t pid, struct process_info *info) {

    uint32_t index;
    
    for (index = 0; process_get_info (index, info); index++) {
    
        if (info->pid == pid) {
            return 1;
        }
    
    }
    
    return 0;

}

static int procfs_pid_readdir (void *data, uint32_t index, struct dirent *de) {

    (void) data;
    
    if (index > 3) {
        return 0;
    }
    
    strcpy (de->d_name, index == 0 ? "." : index == 1 ? ".." : index == 2 ? "status" : "fd");
    
    de->d_ino  = index;
    de->d_off  = index + 1;
    
    de->d_type = index == 2 ? DT_REG : DT_DIR;
    return 1;

}

static int procfs_fd_readdir (void *data, uint32_t index, struct dirent *de) {

    struct fs_node *node = (struct fs_node *) data;
    uint32_t fd;
    
    if (index < 2) {
    
        strcpy (de->d_name, index == 0 ? "." : "..");
        de->d_type = DT_DIR;
    
    } else {
    
        if (!process_get_fd ((uint32_t) (uintptr_t) node->data, index - 2, &fd)) {
            return 0;
        }
        
        sprintf (de->d_name, "%u", (unsigned) fd);
        de->d_type = DT_REG;
    
    }
    
    de->d_ino = index;
    de->d_off = index + 1;
    
    return 1;

}

static int procfs_root_readdir (void *data, uint32_t index, struct dirent *de) {

    struct process_info info;
    struct dirent scratch;
    
    uint32_t fixed = 0;
    
    if (!procfs_mapper_readdir) {
        return 0;
    }
    
    if (procfs_mapper_readdir (data, index, de)) {
        return 1;
    }
    
    while (procfs_mapper_readdir (data, fixed, &scratch)) {
        fixed++;
    }
    
    if (index == fixed) {
        strcpy (de->d_name, "self");
    } else if (index > fixed && process_get_info (index - fixed - 1, &info)) {
        sprintf (de->d_name, "%u", (unsigned) info.pid);
    } else {
        return 0;
    }
    
    de->d_type = DT_DIR;
    
    de->d_ino  = index;
    de->d_off  = index + 1;
    
    return 1;

}

static int procfs_dir_close (void *data) {

    kfree (data);
    return 0;

}

static int procfs_close (void *data) {

    struct fs_node *node = (struct fs_node *) data;
    
    kfree (node->data);
    kfree (node);
    
    return 0;

}

static int procfs_read (void *data, void *buf, size_t count) {

    struct fs_node *node = (struct fs_node *) data;
    struct procfs_file *file = (struct procfs_file *) node->data;
    
    struct thread *thread = thread_get_current ();
    size_t remaining;
    
    if (file->offset >= file->length || !count) {
        return 0;
    }
    
    if (!buf) {
        return -EFAULT;
    }
    
    remaining = file->length - file->offset;
    
    if (count > remaining) {
        count = remaining;
    }
    
    if (thread && thread->user_mode) {
    
        if (!thread->owner || !mmu_copy_user (thread->owner->pd, (uintptr_t) buf, file->text + file->offset, count, 1)) {
            return -EFAULT;
        }
    
    } else {
        memcpy (buf, file->text + file->offset, count);
    }
    
    file->offset += count;
    return (int) count;

}

static off_t procfs_seek (void *data, off_t offset, int whence) {

    struct fs_node *node = (struct fs_node *) data;
    struct procfs_file *file = (struct procfs_file *) node->data;
    
    off_t next;
    
    if (whence == SEEK_SET) {
        next = offset;
    } else if (whence == SEEK_CUR) {
        next = (off_t) file->offset + offset;
    } else if (whence == SEEK_END) {
        next = (off_t) file->length + offset;
    } else {
        return -EINVAL;
    }
    
    if (next < 0) {
        return -EINVAL;
    }
    
    file->offset = (size_t) next;
    return next;

}

static int procfs_stat (void *data, struct stat *st) {

    st->st_size = 0;
    
    (void) data;
    return 0;

}

static struct fs_node *procfs_open_pid (void *data, const char *path, size_t depth) {

    struct fs_node *entry = (struct fs_node *) data, *node;
    
    struct process_info info;
    struct procfs_file *file;
    
    struct thread *thread;
    
    const char *p, *name = 0;
    uint32_t pid = 0, fd = 0, index, current_fd;
    
    if (!depth) {
        return entry;
    }
    
    if (depth > 3 || !*path) {
        return 0;
    }
    
    if (!strcmp (path, "self")) {
    
        thread = thread_get_current ();
        
        if (!thread || !thread->owner) {
            return 0;
        }
        
        pid = thread->owner->pid;
    
    } else {
    
        for (p = path; *p; p++) {
        
            if (*p < '0' || *p > '9' || pid > (UINT32_MAX - (uint32_t) (*p - '0')) / 10) {
                return 0;
            }
            
            pid = pid * 10 + (uint32_t) (*p - '0');
        
        }
    
    }
    
    if (!procfs_lookup (pid, &info)) {
        return 0;
    }
    
    if (depth >= 2) {
    
        name = path + strlen (path) + 1;
        
        if (strcmp (name, "status") && strcmp (name, "fd")) {
            return 0;
        }
    
    }
    
    if (depth == 3) {
    
        if (strcmp (name, "fd")) {
            return 0;
        }
        
        p = name + strlen (name) + 1;
        
        if (!*p) {
            return 0;
        }
        
        for (; *p; p++) {
        
            if (*p < '0' || *p > '9') {
                return 0;
            }
            
            fd = fd * 10 + (uint32_t) (*p - '0');
            
            if (fd >= 256) {
                return 0;
            }
        
        }
        
        for (index = 0;; index++) {
        
            if (!process_get_fd (pid, index, &current_fd)) {
                return 0;
            }
            
            if (current_fd == fd) {
                break;
            }
        
        }
    
    }
    
    if (!(node = kmalloc (sizeof (*node)))) {
        return 0;
    }
    
    memset (node, 0, sizeof (*node));
    
    if (depth == 1 || (depth == 2 && !strcmp (name, "fd"))) {
    
        node->flags   = INO_DIR;
        node->data    = (void *) (uintptr_t) pid;
        node->kclose  = procfs_dir_close;
        node->readdir = depth == 1 ? procfs_pid_readdir : procfs_fd_readdir;
        
        return node;
    
    }
    
    if (!(file = kmalloc (sizeof (*file)))) {
    
        kfree (node);
        return 0;
    
    }
    
    memset (file, 0, sizeof (*file));
    
    if (depth == 2) {
    
        sprintf (file->text, "Name:\t%s\nState:\t%s\nPid:\t%u\nPPid:\t%u\nTid:\t%u\n",
            info.name, procfs_state_name (info.state), (unsigned) info.pid, (unsigned) info.ppid, (unsigned) info.tid);
    
    }
    
    file->length = strlen (file->text);
    
    node->data   = file;
    node->flags  = INO_REG;
    node->kclose = procfs_close;
    node->kread  = procfs_read;
    node->kseek  = procfs_seek;
    node->kstat  = procfs_stat;
    
    return node;

}

static struct fs_node *procfs_open (void *data, const char *path, size_t depth) {

    struct fs_node *entry = (struct fs_node *) data;
    
    struct procfs_file *file;
    struct fs_node *node;
    
    uint64_t uptime, total, free;
    (void) path;
    
    if (depth) {
        return 0;
    }
    
    if (!(node = kmalloc (sizeof (*node)))) {
        return 0;
    }
    
    if (!(file = kmalloc (sizeof (*file)))) {
    
        kfree (node);
        return 0;
    
    }
    
    memset (node, 0, sizeof (*node));
    memset (file, 0, sizeof (*file));
    
    if ((uintptr_t) entry->data == PROC_UPTIME) {
    
        uptime = get_uptime_milliseconds ();
        sprintf (file->text, "%llu.%02llu\n", (uint64_t) (uptime / 1000), (uint64_t) ((uptime % 1000) / 10));
    
    } else if ((uintptr_t) entry->data == PROC_MEMINFO) {
    
        mmu_get_memory_info (&total, &free);
        sprintf (file->text, "MemTotal: %llu kB\nMemFree:  %llu kB\n", total, free);
    
    } else {
        strcpy (file->text, "Corazon\n");
    }
    
    file->length = strlen (file->text);
    
    node->data   = file;
    node->flags  = INO_REG;
    node->kclose = procfs_close;
    node->kread  = procfs_read;
    node->kseek  = procfs_seek;
    node->kstat  = procfs_stat;
    
    return node;

}

static void procfs_create (const char *path, uintptr_t type) {

    struct fs_node *node;
    
    if (!(node = kmalloc (sizeof (*node)))) {
        return;
    }
    
    memset (node, 0, sizeof (*node));
    
    node->flags = INO_REG;
    node->data = (void *) type;
    node->kopen = procfs_open;
    
    if (!vfs_mount (path, node)) {
        kfree (node);
    }

}

void procfs_install (void) {

    struct fs_node *root;
    
    if ((root = vfs_open ("/proc"))) {
    
        procfs_mapper_readdir = root->readdir;
        
        root->readdir = procfs_root_readdir;
        root->kopen = procfs_open_pid;
        
        vfs_close (root);
    
    }
    
    procfs_create ("/proc/meminfo", PROC_MEMINFO);
    procfs_create ("/proc/uptime", PROC_UPTIME);
    //procfs_create ("/proc/version", PROC_VERSION);

}

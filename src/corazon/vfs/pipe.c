/******************************************************************************
 * @file            pipe.c
 *****************************************************************************/
#include    <corazon/utils/misc.h>
#include    <corazon/vfs/pipe.h>
#include    <corazon/vfs/vfs.h>

#include    <sys/signal_defs.h>

#include    <errno.h>
#include    <limits.h>
#include    <string.h>

#define     PIPE_CAPACITY               4096

struct pipe_data {

    struct fs_node reader, writer;
    size_t head, count;
    
    uint8_t buffer[PIPE_CAPACITY];

};

static void pipe_close (void *data) {

    struct fs_node *node = (struct fs_node *) data;
    struct pipe_data *pipe = (struct pipe_data *) node->data;
    
    if (!pipe->reader.refcount && !pipe->writer.refcount) {
        kfree (pipe);
    }

}

static int pipe_poll (void *data) {

    struct fs_node *node = (struct fs_node *) data;
    struct pipe_data *pipe = (struct pipe_data *) node->data;
    
    if (node == &pipe->reader) {
        return (pipe->count || !pipe->writer.refcount) ? VFS_POLLIN : 0;
    }
    
    return (pipe->count < PIPE_CAPACITY || !pipe->reader.refcount) ? VFS_POLLOUT : 0;

}

static int pipe_transfer (struct thread *thread, struct fs_node *node, uintptr_t address, size_t count, int write) {

    struct pipe_data *pipe = (struct pipe_data *) node->data;
    uintptr_t pd = thread->owner->pd;
    
    size_t available, offset, first;
    
    if ((write && node != &pipe->writer) || (!write && node != &pipe->reader)) {
        return -EBADF;
    }
    
    if (!count) {
        return 0;
    }
    
    if (write) {
    
        if (!pipe->reader.refcount) {
        
            if (!thread->pending_signal) {
                thread->pending_signal = SIGPIPE;
            }
            
            return -EPIPE;
        
        }
        
        available = PIPE_CAPACITY - pipe->count;
        
        if (!available || (count <= PIPE_CAPACITY && available < count)) {
            return -EAGAIN;
        }
        
        offset = (pipe->head + pipe->count) % PIPE_CAPACITY;
    
    } else {
    
        if (!pipe->count) {
            return pipe->writer.refcount ? -EAGAIN : 0;
        }
        
        available = pipe->count;
        offset = pipe->head;
    
    }
    
    if (count > available) {
        count = available;
    }
    
    if (!mmu_user_buffer_valid (pd, address, count, !write)) {
        return -EFAULT;
    }
    
    first = PIPE_CAPACITY - offset;
    
    if (first > count) {
        first = count;
    }
    
    if (!mmu_copy_user (pd, address, pipe->buffer + offset, first, !write)) {
        return -EFAULT;
    }
    
    if (count > first && !mmu_copy_user (pd, address + first, pipe->buffer, count - first, !write)) {
        return -EFAULT;
    }
    
    if (write) {
        pipe->count += count;
    } else {
    
        pipe->head = (pipe->head + count) % PIPE_CAPACITY;
        pipe->count -= count;
    
    }
    
    return (int) count;

}

static int pipe_continue (struct thread *thread) {

    struct fs_node *node = thread->pipe.node;
    int ret;
    
    ret = pipe_transfer (thread, node, thread->pipe.address + thread->pipe.written, thread->pipe.length - thread->pipe.written, thread->pipe.write);
    
    if (!thread->pipe.write) {
        return ret;
    }
    
    if (ret < 0) {
    
        if (ret != -EAGAIN && thread->pipe.written) {
            return (int) thread->pipe.written;
        }
        
        return ret;
    
    }
    
    thread->pipe.written += (size_t) ret;
    
    if (thread->pipe.written < thread->pipe.length) {
        return -EAGAIN;
    }
    
    return (int) thread->pipe.written;

}

static int pipe_io (struct fs_node *node, uintptr_t address, size_t count, int write) {

    struct thread *thread = thread_get_current ();
    int ret;
    
    if (count > INT_MAX) {
        return -EINVAL;
    }
    
    if (count && (!address || count > (uintptr_t) -1 - address)) {
        return -EFAULT;
    }
    
    thread->pipe.node = node;
    thread->pipe.write = write;
    thread->pipe.address = address;
    thread->pipe.length = count;
    thread->pipe.written = 0;
    
    ret = pipe_continue (thread);
    
    if (ret == -EAGAIN) {
        thread->state = TS_PIPE;
    } else {
        thread->pipe.node = 0;
    }
    
    return ret;

}

static int pipe_read (void *data, void *buf, size_t count) {
    return pipe_io (data, (uintptr_t) buf, count, 0);
}

static int pipe_write (void *data, void *buf, size_t count) {
    return pipe_io (data, (uintptr_t) buf, count, 1);
}

int kpipe (uintptr_t address) {

    struct process *process = thread_get_current ()->owner;
    struct pipe_data *pipe;
    
    int fds[2], i, found = 0;
    
    if (!mmu_user_buffer_valid (process->pd, address, sizeof (fds), 1)) {
        return -EFAULT;
    }
    
    for (i = 0; i < 256 && found < 2; i++) {
    
        if (!process->fd[i]) {
            fds[found++] = i;
        }
    
    }
    
    if (found != 2) {
        return -EMFILE;
    }
    
    if (!(pipe = kmalloc (sizeof (*pipe)))) {
        return -ENOMEM;
    }
    
    memset (pipe, 0, sizeof (*pipe));
    
    pipe->reader.refcount = 1;
    pipe->reader.flags = INO_FIFO;
    pipe->reader.data = pipe;
    pipe->reader.kclose = pipe_close;
    pipe->reader.kread = pipe_read;
    pipe->reader.kpoll = pipe_poll;
    
    pipe->writer.refcount = 1;
    pipe->writer.flags = INO_FIFO;
    pipe->writer.data = pipe;
    pipe->writer.kclose = pipe_close;
    pipe->writer.kwrite = pipe_write;
    pipe->writer.kpoll = pipe_poll;
    
    if (!mmu_copy_user (process->pd, address, fds, sizeof (fds), 1)) {
    
        kfree (pipe);
        return -EFAULT;
    
    }
    
    process->fd[fds[0]] = &pipe->reader;
    process->fd[fds[1]] = &pipe->writer;
    
    process->dir_index[fds[0]] = 0;
    process->dir_index[fds[1]] = 0;
    
    return 0;

}

void kpipe_update (struct thread *thread) {

    int ret;
    
    if (thread->state != TS_PIPE) {
        return;
    }
    
    ret = pipe_continue (thread);
    
    if (ret == -EAGAIN) {
        return;
    }
    
    thread->pipe.node = 0;
    thread->state = TS_RUN;
    
#if     defined (__AMD64__)
    thread->regs.rax = (uintptr_t) ret;
#else
    thread->regs.eax = (uintptr_t) ret;
#endif

}

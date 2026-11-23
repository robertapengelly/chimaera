/******************************************************************************
 * @file            process.c
 *****************************************************************************/
#include    <corazon/tasking/irq.h>
#include    <corazon/tasking/process.h>
#include    <corazon/utils/misc.h>
#include    <corazon/vfs/pipe.h>
#include    <corazon/vfs/vfs.h>
#include    <corazon/console.h>
#include    <corazon/cpu.h>
#include    <corazon/mmu.h>
#include    <corazon/regs.h>
#include    <corazon/timer.h>
#include    <corazon/tss.h>

#include    <sys/signal_defs.h>

#include    <errno.h>
#include    <stdint.h>
#include    <string.h>

static struct process *kernel_process = 0;

static struct thread *g_first_thread = 0;
struct thread *g_current_thread = 0;

static uint32_t process_id_generator = 0;
static uint32_t thread_id_generator = 0;

struct ipc_request_attempt {

    uint32_t pid;
    struct ipc_request_attempt *next;

};

struct execve_data;

struct ipc_request {

    ipc_message_t message;
    struct thread *sender;
    
    uintptr_t reply_address;
    uint32_t handler_pid;
    
    struct execve_data *execve;
    int wait_reply;
    
    unsigned redirects;
    
    struct ipc_request_attempt *attempts;
    struct ipc_request *next;

};

#define     EXECVE_MAX_STRINGS          128
#define     EXECVE_MAX_REDIRECTS        4

#define     EXECVE_MAX_BYTES            (PAGE_SIZE * 4)
#define     EXECVE_STACK_SIZE           (PAGE_SIZE * 8)

#define     EXECVE_STACK_TOP            0x70000000UL
#define     EXECVE_STACK_BOTTOM         (EXECVE_STACK_TOP - EXECVE_STACK_SIZE)

#define     NT_PARAMETERS_ADDRESS       0x7FFC0000UL

#define     NT_TEB_ADDRESS              0x7FFDE000UL
#define     NT_PEB_ADDRESS              0x7FFDF000UL

struct execve_data {

    size_t argc, envc, bytes;
    
    size_t argv[EXECVE_MAX_STRINGS];
    size_t envp[EXECVE_MAX_STRINGS];
    
    char *strings;

};

static int process_exec_loader_start_buffers (const ipc_buffer_t *buffers, size_t count, uintptr_t entry_point, uint32_t exec_type, const uintptr_t *data, size_t data_count);
static int process_create_user_task_at_buffers (const ipc_buffer_t *buffers, size_t count, uintptr_t entry_point, uint32_t exec_type, const uintptr_t *data, size_t data_count, const char *path);

static struct ipc_request *ipc_requests = 0;
static void process_reap_zombies (struct thread *current);

static void process_dispatch_signals (void);

static void thread_resume (struct thread *thread) {

    thread->state = TS_RUN;
    thread->state_private_data = 0;

}

static uint32_t generate_process_id () {
    return process_id_generator++;
}

static uint32_t generate_thread_id () {
    return thread_id_generator++;
}

static void thread_update_usage_metrics (void) {
    //
}

extern void kselect_update (struct thread *thread);

static void thread_update_state (struct thread *thread) {

    kselect_update (thread);
    kpipe_update (thread);
    
    if (thread->pending_signal == SIGPIPE) {
        process_dispatch_signals ();
    }

}

static struct thread *look_threads (struct thread *current) {

    struct thread *t = current->next;

    while (t) {

        thread_update_state (t);

        if (t->state == TS_RUN) {
            return t;
        }
        
        if (t->state == TS_SLEEP && t->wake > 0) {
        
            if (t->wake <= get_uptime_milliseconds ()) {
            
                t->state = TS_RUN;
                t->wake = 0;
                
                return t;
            
            }
        
        }

        t = t->next;

    }

    t = g_first_thread;

    while (t != current) {

        thread_update_state (t);

        if (t->state == TS_RUN) {
            return t;
        }
        
        if (t->state == TS_SLEEP && t->wake > 0) {
        
            if (t->wake <= get_uptime_milliseconds ()) {
            
                t->state = TS_RUN;
                t->wake = 0;
                
                return t;
            
            }
        
        }
        
        t = t->next;
    
    }
    
    if (current->state == TS_RUN) {
        return current;
    }
    
    return g_first_thread;

}

extern void thread_switch (struct regs *regs, int mode);

extern uintptr_t syscall_stack;
extern tss_entry_t *g_tss;

#if     defined (__AMD64__)
extern void cpu_msr_write (uint32_t msr, uint64_t value);

static void end_context (struct regs *regs, struct thread *thread) {

    thread->regs.rflags = regs->rflags;
    thread->regs.cs = (uint16_t) regs->cs;
    
    thread->regs.rip = regs->rip;
    thread->regs.rax = regs->rax;
    thread->regs.rbx = regs->rbx;
    thread->regs.rcx = regs->rcx;
    thread->regs.rdx = regs->rdx;
    thread->regs.rsi = regs->rsi;
    thread->regs.rdi = regs->rdi;
    thread->regs.rbp = regs->rbp;
    thread->regs.r8  = regs->r8;
    thread->regs.r9  = regs->r9;
    thread->regs.r10 = regs->r10;
    thread->regs.r11 = regs->r11;
    thread->regs.r12 = regs->r12;
    thread->regs.r13 = regs->r13;
    thread->regs.r14 = regs->r14;
    thread->regs.r15 = regs->r15;
    
    thread->regs.rsp = regs->rsp;
    thread->regs.ss = regs->ss;
    
    if (thread->regs.cs == 0x1B) {
    
        uint16_t fs;
        
        __asm__ __volatile__ (
            "mov %0, fs"
            : "=r" (fs)
        );
        
        thread->regs.fs = fs;
    
    }

}

static void thread_switch_to (struct thread *thread, int mode) {

    struct regs *regs;
    uintptr_t rsp;
    
    if (thread->kstack.rsp0) {
    
        g_tss->rsp[0] = thread->kstack.rsp0;
        syscall_stack = thread->kstack.rsp0;
    
    }
    
    /* Switch address spaces. */
    if (thread->owner->pd != kernel_page_directory_physical) {
    
        __asm__ __volatile__ (
            "mov cr3, %0"
            :
            : "r" (thread->owner->pd)
        );
    
    } else {
    
        __asm__ __volatile__ (
            "mov cr3, %0"
            :
            : "r" (kernel_page_directory_physical)
        );
    
    }
    
    __asm__ __volatile__ (
        "mov ds, ax"
        :
        : "a" ((uint16_t) thread->regs.ds)
    );
    
    __asm__ __volatile__ (
        "mov es, ax"
        :
        : "a" ((uint16_t) thread->regs.es)
    );
    
    /* Set FS.base for this thread's TLS. */
    __asm__ __volatile__ (
        "mov fs, ax"
        :
        : "a" ((uint16_t) thread->regs.fs)
    );
    
    cpu_msr_write (0xC0000100, thread->tls_base);
    cpu_msr_write (0xC0000102, thread->nt_teb_base);
    
    /**
     * User mode returns from the kernel stack. Kernel mode returns from
     * the thread's saved kernel stack.
     */
    if (mode) {
        rsp = thread->kstack.rsp0;
    } else {
        rsp = thread->regs.rsp;
    }
    
    rsp -= sizeof (*regs);
    regs = (struct regs *) rsp;
    
    regs->r15 = thread->regs.r15;
    regs->r14 = thread->regs.r14;
    regs->r13 = thread->regs.r13;
    regs->r12 = thread->regs.r12;
    regs->r11 = thread->regs.r11;
    regs->r10 = thread->regs.r10;
    regs->r9  = thread->regs.r9;
    regs->r8  = thread->regs.r8;
    
    regs->rbp = thread->regs.rbp;
    regs->rdi = thread->regs.rdi;
    regs->rsi = thread->regs.rsi;
    regs->rdx = thread->regs.rdx;
    regs->rcx = thread->regs.rcx;
    regs->rbx = thread->regs.rbx;
    regs->rax = thread->regs.rax;
    
    regs->int_no = 0;
    regs->err_code = 0;
    
    regs->rip = thread->regs.rip;
    regs->cs = thread->regs.cs;
    
    regs->rflags = thread->regs.rflags;
    regs->rflags |= 0x200;
    regs->rflags &= 0xFFFFFFFFFFFFBFFFULL;
    
    regs->rsp = thread->regs.rsp;
    regs->ss = thread->regs.ss;
    
    thread_switch (regs, mode);

}
#else
static void end_context (struct regs *regs, struct thread *thread) {

    thread->regs.eflags = regs->eflags;
    thread->regs.cs = (uint16_t) regs->cs;
    
    thread->regs.eip = regs->eip;
    thread->regs.eax = regs->eax;
    thread->regs.ebx = regs->ebx;
    thread->regs.ecx = regs->ecx;
    thread->regs.edx = regs->edx;
    thread->regs.esi = regs->esi;
    thread->regs.edi = regs->edi;
    thread->regs.ebp = regs->ebp;
    
    thread->regs.ds  = (uint16_t) regs->ds;
    thread->regs.es  = (uint16_t) regs->es;
    thread->regs.fs  = (uint16_t) regs->fs;
    thread->regs.gs  = (uint16_t) regs->gs;
    
    if (thread->regs.cs != 0x08) {
    
        thread->regs.esp = regs->useresp;
        thread->regs.ss = (uint16_t) regs->ss;
    
    } else {
    
        thread->regs.esp = (uintptr_t) &regs->useresp;
        thread->regs.ss = (uint16_t) g_tss->ss0;
    
    }
    
    thread->kstack.ss0 = (uint16_t) g_tss->ss0;
    thread->kstack.esp0 = g_tss->esp0;

}

static void thread_switch_to (struct thread *thread, int mode) {

    struct regs *regs;
    uintptr_t esp;
    
    /* Restore the kernel stack used when entering ring 0. */
    g_tss->ss0 = thread->kstack.ss0;
    g_tss->esp0 = thread->kstack.esp0;
    
    gdt_set_tls (thread->tls_base, thread->tls_limit, thread->tls_access, thread->tls_flags);
    
    /* Switch address spaces. */
    if (thread->owner->pd != kernel_page_directory_physical) {
    
        __asm__ __volatile__ (
            "mov cr3, %0"
            :
            : "r" (thread->owner->pd)
        );
    
    } else {
    
        __asm__ __volatile__ (
            "mov cr3, %0"
            :
            : "r" (kernel_page_directory_physical)
        );
    
    }
    
    /**
     * User mode returns from the kernel stack. Kernel mode returns from
     * the thread's saved kernel stack.
     */
    if (mode) {
        esp = thread->kstack.esp0;
    } else {
        esp = thread->regs.esp;
    }
    
    if (mode) {
        esp -= sizeof (*regs);
    } else {
        esp -= offsetof (struct regs, useresp);
    }
    
    regs = (struct regs *) esp;
    
    regs->gs = thread->regs.gs;
    regs->fs = thread->regs.fs;
    regs->es = thread->regs.es;
    regs->ds = thread->regs.ds;
    
    regs->edi = thread->regs.edi;
    regs->esi = thread->regs.esi;
    regs->ebp = thread->regs.ebp;
    regs->esp = thread->regs.esp;
    regs->ebx = thread->regs.ebx;
    regs->edx = thread->regs.edx;
    regs->ecx = thread->regs.ecx;
    regs->eax = thread->regs.eax;
    
    regs->int_no = 0;
    regs->err_code = 0;
    
    regs->eip = thread->regs.eip;
    regs->cs = thread->regs.cs;
    
    regs->eflags = (thread->regs.eflags | 0x200) & 0xFFFFBFFF;
    
    if (mode) {
    
        regs->useresp = thread->regs.esp;
        regs->ss = thread->regs.ss;
    
    }
    
    thread_switch (regs, mode);

}
#endif

extern uint64_t get_uptime_milliseconds (void);

static void start_context (struct thread *thread) {

    //g_previous_scheduled_thread = g_current_thread;
    
    g_current_thread = thread;
    
    //g_system_context_switch_count++;
    thread->context_switch_count++;
    
    if (thread->regs.cs != 0x08) {
        thread_switch_to (thread, 1);
    } else {
        thread_switch_to (thread, 0);
    }
    
    thread->context_start_time = get_uptime_milliseconds ();

}

struct thread *thread_get_current (void) {
    return g_current_thread;
}

int process_get_fd (uint32_t pid, uint32_t index, uint32_t *fd) {

    struct thread *thread;
    struct process *process = 0;
    
    uintptr_t flags;
    uint32_t i;
    
    int found = 0;
    
    if (!fd) {
        return 0;
    }
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("pushfq");
#else
    __asm__ __volatile__ ("pushfd");
#endif
    
    __asm__ __volatile__ ("pop %0" : "=r" (flags) : : "memory");
    __asm__ __volatile__ ("cli");
    
    for (thread = g_first_thread; thread; thread = (struct thread *) thread->next) {
    
        if (thread->owner && thread->owner->pid == pid) {
        
            process = thread->owner;
            break;
        
        }
    
    }
    
    if (process) {
    
        for (i = 0; i < 256; i++) {
        
            if (!process->fd[i]) {
                continue;
            }
            
            if (index--) {
                continue;
            }
            
            *fd = i;
            
            found = 1;
            break;
        
        }
    
    }
    
    __asm__ __volatile__ ("push %0" : : "r" (flags) : "memory");
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("popfq");
#else
    __asm__ __volatile__ ("popfd");
#endif
    
    return found;

}

int process_get_info (uint32_t index, struct process_info *info) {

    struct thread *thread, *previous;
    
    struct process *process;
    uintptr_t flags;
    
    int found = 0;
    
    if (!info) {
        return 0;
    }
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("pushfq");
#else
    __asm__ __volatile__ ("pushfd");
#endif
    
    __asm__ __volatile__ ("pop %0" : "=r" (flags) : : "memory");
    __asm__ __volatile__ ("cli");
    
    for (thread = g_first_thread; thread; thread = (struct thread *) thread->next) {
    
        process = thread->owner;
        
        if (!process) {
            continue;
        }
        
        for (previous = g_first_thread; previous != thread; previous = (struct thread *) previous->next) {
        
            if (previous->owner == process) {
                break;
            }
        
        }
        
        if (previous != thread) {
            continue;
        }
        
        if (index--) {
            continue;
        }
        
        memcpy (info->name, process->name, sizeof (info->name));
        
        info->pid = process->pid;
        info->ppid = process->parent ? process->parent->pid : 0;
        info->tid = thread->threadId;
        info->state = thread->state;
        
        found = 1;
        break;
    
    }
    
    __asm__ __volatile__ ("push %0" : : "r" (flags) : "memory");

#if     defined (__AMD64__)
    __asm__ __volatile__ ("popfq");
#else
    __asm__ __volatile__ ("popfd");
#endif
    
    return found;

}

#if     0
#if     defined (__AMD64__)
uintptr_t thread_get_kernel_stack (void) {
    return g_current_thread->kstack.rsp0;
}
#endif
#endif

void tasking_initialize (void) {

    struct process *process;
    struct thread *thread;
    
    uint32_t selector;
    
    if (!(process = kmalloc (sizeof (*process)))) {
    
        kprintf ("panic: not enough memory for kernel process\n");
        
        for (;;) {
        
#if     defined (__AMD64__)
            __asm__ __volatile__ ("pause");
#else
            __asm__ __volatile__ ("hlt");
#endif
        
        }
    
    }
    
    memset (process, 0, sizeof (*process));
    
    if (!(process->console = console_create ())) {
    
        kprintf ("panic: not enough memory for kernel console\n");
        
        for (;;) {
        
#if     defined (__AMD64__)
            __asm__ __volatile__ ("pause");
#else
            __asm__ __volatile__ ("hlt");
#endif
        
        }
    
    }
    
    console_switch (process->console);
    
    strcpy (process->name, "[idle]");
    strcpy (process->cwd, "/");
    
    process->pid = generate_process_id ();
    process->pd = kernel_page_directory_physical;
    
    kernel_process = process;
    
    if (!(thread = kmalloc (sizeof (*thread)))) {
    
        kprintf ("panic: not enough memory for kernel thread\n");
        
        for (;;) {
        
#if     defined (__AMD64__)
            __asm__ __volatile__ ("pause");
#else
            __asm__ __volatile__ ("hlt");
#endif
        
        }
    
    }
    
    memset (thread, 0, sizeof (*thread));
    
    thread->owner = kernel_process;
    thread->threadId = generate_thread_id ();
    
    thread->user_mode = 0;
    thread_resume (thread);
    
    thread->birth_time = get_uptime_milliseconds ();
    
    thread->regs.cr3 = process->pd;
    selector = 0x10;
    
    thread->regs.ss = selector;
    thread->regs.cs = 0x08;
    
#if     defined (__AMD64__)
    thread->regs.rflags = 0;
    thread->regs.rip = 0;
#else
    thread->regs.eflags = 0;
    thread->regs.eip = 0;
#endif
    
    thread->regs.ds = selector;
    thread->regs.es = selector;
    thread->regs.fs = selector;
    thread->regs.gs = selector;
    
#if     defined (__AMD64__)
    thread->regs.rsp = 0;
#else
    thread->regs.esp = 0;
#endif
    
    thread->tls_base = 0;
    
#if     defined (__AMD64__)
    thread->kstack.rsp0 = 0;
#else
    thread->tls_limit = 0xFFFFFFFF;
    
    thread->tls_access = 0xF2;
    thread->tls_flags = 0x0C;
    
    thread->kstack.ss0 = 0x10;
    thread->kstack.esp0 = 0;
#endif
    
    g_first_thread = thread;
    g_current_thread = thread;

}

struct thread *thread_create_kthread (void (*func) (void)) {

    struct thread *thread, *p;
    
    uint8_t *stack;
    uint32_t selector;
    
    if (!(thread = kmalloc (sizeof (*thread)))) {
    
        dprintf ("error: not enough memory to create a new thread\n");
        return 0;
    
    }
    
    memset (thread, 0, sizeof (*thread));
    
    thread->owner = kernel_process;
    thread->threadId = generate_thread_id ();
    
    thread->user_mode = 0;
    thread_resume (thread);
    
    thread->birth_time = get_uptime_milliseconds ();
    thread->birth_time = 0;
    
    thread->regs.cr3 = thread->owner->pd;
    selector = 0x10;
    
    thread->regs.ss = selector;
    thread->regs.cs = 0x08;
    
#if     defined (__AMD64__)
    thread->regs.rflags = 0;
    thread->regs.rip = (uint64_t ) func;
#else
    thread->regs.eflags = 0;
    thread->regs.eip = (uint32_t) func;
#endif
    
    thread->regs.ds = selector;
    thread->regs.es = selector;
    thread->regs.fs = selector;
    thread->regs.gs = selector;
    
    if (!(stack = kmalloc (0x8000))) {
    
        dprintf ("error: not enough memory for a stack\n");
        
        kfree (thread);
        return 0;
    
    }
    
    memset (stack, 0, 0x8000);
    
#if     defined (__AMD64__)
    thread->regs.rsp = (uint64_t) (stack + 0x8000 - 8);
#else
    thread->regs.esp = (uint32_t) (stack + 0x8000 - 4);
#endif
    
    thread->tls_base = 0;
    
#if     defined (__AMD64__)
    thread->kstack.rsp0 = 0;
#else
    thread->tls_limit = 0xFFFFFFFF;
    
    thread->tls_access = 0xF2;
    thread->tls_flags = 0x0C;
    
    thread->kstack.ss0 = 0x10;
    thread->kstack.esp0 = 0;
#endif
    
    thread->kstack.stack_start = (uintptr_t) stack;
    p = g_current_thread;
    
    while (p->next) { p = p->next; }
    p->next = thread;
    
    return thread;

}

int thread_destroy_kthread (struct thread *thread) {

    struct thread *previous;
    uintptr_t flags;
    
    if (!thread || thread == g_current_thread || thread == g_first_thread || thread->user_mode || thread->owner != kernel_process) {
        return -EINVAL;
    }
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("pushfq");
#else
    __asm__ __volatile__ ("pushfd");
#endif
    
    __asm__ __volatile__ ("pop %0" : "=r" (flags) : : "memory");
    __asm__ __volatile__ ("cli");
    
    previous = g_first_thread;
    
    while (previous && (struct thread *) previous->next != thread) {
        previous = (struct thread *) previous->next;
    }
    
    if (!previous) {
        goto done;
    }
    
    previous->next = thread->next;
    
    kfree ((void *) thread->kstack.stack_start);
    kfree (thread);
    
done:

    __asm__ __volatile__ ("push %0" : : "r" (flags) : "memory");
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("popfq");
#else
    __asm__ __volatile__ ("popfd");
#endif
    
    return previous ? 0 : -ESRCH;

}

void schedule (struct regs *regs) {

    struct thread *current = g_current_thread, *ready_thread;
    
    if (current) {
    
        process_reap_zombies (current);
        
        if (current->state == TS_SLEEP && current->wake > 0 && current->wake <= get_uptime_milliseconds ()) {
        
            current->state = TS_RUN;
            current->wake = 0;
        
        }
        
        if (!current->next && current == g_first_thread) {
            return;
        }
        
        if (current->state == TS_UNINTERRUPTIBLE) {
            return;
        }
        
        end_context (regs, current);
        
        if ((current->regs.cs & 3) == 3 || current == g_first_thread) {
            process_dispatch_signals ();
        }
        
        ready_thread = look_threads (current);
    
    } else {
        ready_thread = look_threads (g_first_thread);
    }
    
    if (ready_thread != g_first_thread) {
        //
    }
    
    thread_update_usage_metrics ();
    
    if (regs->int_no == 32) {
        irq_ack ();
    }
    
    start_context (ready_thread);

}

/* Resolve an IPC endpoint to its current thread. */
static struct thread *ipc_find_thread (uint32_t pid) {

    struct thread *thread;
    
    for (thread = g_first_thread; thread; thread = (struct thread *) thread->next) {
    
        if (thread->owner && thread->owner->pid == pid) {
            return thread;
        }
    
    }
    
    return 0;

}

static void ipc_set_result (struct regs *regs, intptr_t result) {

    if (!regs) {
        return;
    }
    
#if     defined (__AMD64__)
    regs->rax = (uintptr_t) result;
#else
    regs->eax = (uintptr_t) result;
#endif

}

/* Copy a message and its variable-length data into kernel-owned memory. */
static int ipc_copy_message (struct thread *sender, const ipc_message_t *message, ipc_message_t *copy) {

    uintptr_t *data;
    size_t data_size;
    
    ipc_buffer_t *buffers;
    size_t buffers_size;
    
    void *buffer;
    size_t i;
    
    int error;
    
    if (sender->user_mode) {
    
        if (!mmu_copy_user (sender->owner->pd, (uintptr_t) message, copy, sizeof (*copy), 0)) {
            return -EFAULT;
        }
    
    } else {
        memcpy (copy, message, sizeof (*copy));
    }
    
    buffers = copy->buffers;
    data = copy->data;
    buffer = copy->buffer;
    
    copy->source = sender->owner->pid;
    
    copy->buffers = 0;
    copy->data = 0;
    copy->buffer = 0;
    
    if (copy->data_count) {
    
        if (!data || copy->data_count > (size_t) -1 / sizeof (*data)) {
        
            error = -EFAULT;
            goto failed;
        
        }
        
        data_size = copy->data_count * sizeof (*data);
        
        if (!(copy->data = kmalloc (data_size))) {
        
            error = -ENOMEM;
            goto failed;
        
        }
        
        if (sender->user_mode) {
        
            if (!mmu_copy_user (sender->owner->pd, (uintptr_t) data, copy->data, data_size, 0)) {
                
                error = -EFAULT;
                goto failed;
            
            }
        
        } else {
            memcpy (copy->data, data, data_size);
        }
    
    }
    
    if (copy->buffers_count) {
    
        if (!buffers || copy->buffers_count > (size_t) -1 / sizeof (*buffers)) {
        
            error = -EINVAL;
            goto failed;
        
        }
        
        buffers_size = copy->buffers_count * sizeof (*buffers);
        
        if (!(copy->buffers = kmalloc (buffers_size))) {
        
            error = -ENOMEM;
            goto failed;
        
        }
        
        memset (copy->buffers, 0, buffers_size);
       
        for (i = 0; i < copy->buffers_count; i++) {
        
            ipc_buffer_t descriptor;
            
            void *source;
            size_t size;
            
            if (sender->user_mode) {
            
                if (!mmu_copy_user (sender->owner->pd, (uintptr_t) (buffers + i), &descriptor, sizeof (descriptor), 0)) {
                        
                    error = -EFAULT;
                    goto failed;
                
                }
            
            } else {
                descriptor = buffers[i];
            }
            
            source = descriptor.data;
            size = descriptor.size;
            
            copy->buffers[i].size = size;
            copy->buffers[i].address = descriptor.address;
            
            if (!source || !size || size > 0x30000000 || size > (size_t) -1 - (PAGE_SIZE - 1)) {
                
                error = -EINVAL;
                goto failed;
            
            }
            
            if (!(copy->buffers[i].data = kmalloc (size))) {
                
                error = -ENOMEM;
                goto failed;
            
            }
            
            if (sender->user_mode) {
            
                if (!mmu_copy_user (sender->owner->pd, (uintptr_t) source, copy->buffers[i].data, size, 0)) {
                        
                    error = -EFAULT;
                    goto failed;
                
                }
            
            } else {
                memcpy (copy->buffers[i].data, source, size);
            }
        
        }
    
    }
    
    if (copy->size) {
    
        if (!buffer || copy->size > (size_t) -1 - (PAGE_SIZE - 1)) {
        
            error = -EINVAL;
            goto failed;
        
        }
        
        if (!(copy->buffer = kmalloc (copy->size))) {
        
            error = -ENOMEM;
            goto failed;
        
        }
        
        if (sender->user_mode) {
        
            if (!mmu_copy_user (sender->owner->pd, (uintptr_t) buffer, copy->buffer, copy->size, 0)) {
                goto failed;
            }
        
        } else {
            memcpy (copy->buffer, buffer, copy->size);
        }
    
    }
    
    return 0;

failed:
    
    if (copy->data) {
        kfree (copy->data);
    }
    
    if (copy->buffer) {
        kfree (copy->buffer);
    }
    
    if (copy->buffers) {
    
        for (i = 0; i < copy->buffers_count; i++) {
        
            if (copy->buffers[i].data) {
                kfree (copy->buffers[i].data);
            }
        
        }
        
        kfree (copy->buffers);
    
    }
    
    return error;

}

/* Copy a message into the receiving process, preserving its buffer pointer. */
static int ipc_deliver (struct thread *receiver, uintptr_t address, const ipc_message_t *message) {

    ipc_message_t target, copy;
    
    ipc_buffer_t *buffers;
    size_t i;
    
    if (receiver->user_mode) {
    
        if (!mmu_copy_user (receiver->owner->pd, address, &target, sizeof (target), 0)) {
            return 0;
        }
    
    } else {
        memcpy (&target, (void *) address, sizeof (target));
    }
    
    if (message->data_count > target.data_capacity || message->data_count > (size_t) -1 / sizeof (*message->data) || (message->data_count && (!message->data || !target.data))) {
        return 0;
    }
    
    if (message->size > target.buffer_size || (message->size && (!message->buffer || !target.buffer))) {
        return 0;
    }
    
    if (message->buffers_count > target.buffers_capacity || message->buffers_count > (size_t) -1 / sizeof (*message->buffers) ||
        (message->buffers_count && (!message->buffers || !target.buffers))) {
        return 0;
    }
    
    if (message->data_count) {
    
        size_t data_size = message->data_count * sizeof (*message->data);
        
        if (receiver->user_mode) {
        
            if (!mmu_copy_user (receiver->owner->pd, (uintptr_t) target.data, message->data, data_size, 1)) {
                return 0;
            }
        
        } else {
            memcpy (target.data, message->data, data_size);
        }
    
    }
    
    if (message->size) {
    
        if (receiver->user_mode) {
        
            if (!mmu_copy_user (receiver->owner->pd, (uintptr_t) target.buffer, message->buffer, message->size, 1)) {
                return 0;
            }
        
        } else {
            memcpy (target.buffer, message->buffer, message->size);
        }
    
    }
    
    if (message->type == IPC_MESSAGE_LOAD && message->size && receiver->user_mode) {
    
        uint8_t first = 0;
        int copied = mmu_copy_user (receiver->owner->pd, (uintptr_t) target.buffer, &first, 1, 0);
        
        dprintf ("[ipc-deliver] pid=%u size=%lu src=%x dst=%x copied=%d\n",
            receiver->owner->pid, (unsigned long) message->size,
                ((const uint8_t *) message->buffer)[0], first, copied);
    
    }
    
    if (message->buffers_count) {
    
        size_t bytes = message->buffers_count * sizeof (*buffers);
        
        if (!(buffers = kmalloc (bytes))) {
            return 0;
        }
        
        if (receiver->user_mode) {
        
            if (!mmu_copy_user (receiver->owner->pd, (uintptr_t) target.buffers, buffers, bytes, 0)) {
            
                kfree (buffers);
                return 0;
            
            }
        
        } else {
            memcpy (buffers, target.buffers, bytes);
        }
        
        for (i = 0; i < message->buffers_count; i++) {
        
            if (!message->buffers[i].data || !buffers[i].data || message->buffers[i].size > buffers[i].size) {
            
                kfree (buffers);
                return 0;
            
            }
            
            if (receiver->user_mode) {
            
                if (!mmu_copy_user (receiver->owner->pd, (uintptr_t) buffers[i].data, message->buffers[i].data, message->buffers[i].size, 1)) {
                
                    kfree (buffers);
                    return 0;
                
                }
            
            } else {
                memcpy (buffers[i].data, message->buffers[i].data, message->buffers[i].size);
            }
            
            buffers[i].size = message->buffers[i].size;
            buffers[i].address = message->buffers[i].address;
        
        }
        
        if (receiver->user_mode) {
        
            if (!mmu_copy_user (receiver->owner->pd, (uintptr_t) target.buffers, buffers, bytes, 1)) {
            
                kfree (buffers);
                return 0;
            
            }
        
        } else {
            memcpy (target.buffers, buffers, bytes);
        }
        
        kfree (buffers);
    
    }
    
    copy = *message;
    
    copy.data = target.data;
    copy.data_capacity = target.data_capacity;
    
    copy.buffers = target.buffers;
    copy.buffers_capacity = target.buffers_capacity;
    
    copy.buffer = target.buffer;
    copy.buffer_size = target.buffer_size;
    
    if (receiver->user_mode) {
        return mmu_copy_user (receiver->owner->pd, address, &copy, sizeof (copy), 1);
    }
    
    memcpy ((void *) address, &copy, sizeof (copy));
    return 1;

}

static void ipc_discard_message (ipc_message_t *message) {

    size_t i;
    
    if (message->buffers) {
    
        for (i = 0; i < message->buffers_count; i++) {
        
            if (message->buffers[i].data) {
                kfree (message->buffers[i].data);
            }
        
        }
        
        kfree (message->buffers);
        message->buffers = 0;
    
    }
    
    if (message->data) {
    
        kfree (message->data);
        message->data = 0;
    
    }
    
    if (message->buffer) {
    
        kfree (message->buffer);
        message->buffer = 0;
    
    }

}

/* Offer a pending message to one process waiting for its type. */
static void ipc_dispatch_requests (void) {

    struct ipc_request_attempt *attempt;
    struct ipc_request *request;
    
    struct thread *receiver;
    
    for (request = ipc_requests; request; request = request->next) {
    
        if (request->handler_pid) {
            return;
        }
        
        for (receiver = g_first_thread; receiver; receiver = (struct thread *) receiver->next) {
        
            if (!receiver->owner || !receiver->ipc_receiving || receiver->ipc_sending || receiver->ipc_receive_from != IPC_MESSAGE_ANY || (receiver->ipc_receive_type && receiver->ipc_receive_type != request->message.type)) {
                continue;
            }
            
            for (attempt = request->attempts; attempt; attempt = attempt->next) {
            
                if (attempt->pid == receiver->owner->pid) {
                    break;
                }
            
            }
            
            if (attempt) {
                continue;
            }
            
            if (!(attempt = kmalloc (sizeof (*attempt)))) {
                continue;
            }
            
            if (!ipc_deliver (receiver, receiver->ipc_receive_address, &request->message)) {
            
                kfree (attempt);
                continue;
            
            }
            
            attempt->pid = receiver->owner->pid;
            attempt->next = request->attempts;
            request->attempts = attempt;
            request->handler_pid = receiver->owner->pid;
            
            receiver->ipc_receiving = 0;
            receiver->ipc_receive_address = 0;
            receiver->ipc_receive_type = 0;
            
            thread_resume (receiver);
            return;
        
        }
    
    }

}

static int process_execve_redirect (struct ipc_request *request, const ipc_message_t *message) {

    struct execve_data *data = request->execve;
    struct ipc_request_attempt *attempt;
    
    const char *path = message->buffer;
    size_t length, tail, prefix, i;
    
    if (!data || !path || message->size < 2 || path[0] != '/' || path[message->size - 1]) {
        return -ENOEXEC;
    }
    
    if (request->redirects == EXECVE_MAX_REDIRECTS) {
        return -ELOOP;
    }
    
    if (message->size > EXECVE_MAX_BYTES - data->bytes) {
        return -E2BIG;
    }
    
    length = strlen (path) + 1;
    prefix = 2;
    
    if (length < message->size) {
    
        if (length + strlen (path + length) + 1 != message->size || !path[length]) {
            return -ENOEXEC;
        }
        
        prefix++;
    
    }
    
    tail = data->argc ? data->argc - 1 : 0;
    
    if (tail + prefix >= EXECVE_MAX_STRINGS) {
        return -E2BIG;
    }
    
    for (i = tail; i; i--) {
        data->argv[i + prefix - 1] = data->argv[i];
    }
    
    data->argv[0] = data->bytes;
    data->argv[prefix - 1] = (const char *) request->message.buffer - data->strings;
    
    if (prefix == 3) {
        data->argv[1] = data->bytes + length;
    }
    
    memcpy (data->strings + data->bytes, path, message->size);
    
    request->message.buffer = data->strings + data->bytes;
    request->message.size = length;
    
    data->bytes += message->size;
    data->argc = tail + prefix;
    
    while (request->attempts) {
    
        attempt = request->attempts;
        request->attempts = attempt->next;
        
        kfree (attempt);
    
    }
    
    request->redirects++;
    request->handler_pid = 0;
    
    ipc_dispatch_requests ();
    return 0;

}

/* A rejected message is offered to the next process waiting for its type. */
static int ipc_reply_to_request (uint32_t destination, const ipc_message_t *message, struct thread *sender) {

    struct ipc_request *request, *previous;
    struct ipc_request_attempt *attempt;
    
    ipc_message_t reply = { 0 };
    
    uintptr_t reply_data[2];
    int result;
    
    previous = 0;
    
    for (request = ipc_requests; request; request = request->next) {
    
        if (request->handler_pid == sender->owner->pid && request->message.source == destination && request->message.type == message->type) {
            break;
        }
        
        previous = request;
    
    }
    
    if (!request) {
        return -ESRCH;
    }
    
    if (message->data_count && message->data[0] == IPC_REDIRECT) {
    
        if (!(result = process_execve_redirect (request, message))) {
            return 0;
        }
        
        reply_data[0] = IPC_NOTFOUND;
        reply_data[1] = (uintptr_t) result;
        
        reply.type = message->type;
        reply.data = reply_data;
        reply.data_count = 2;
        
        message = &reply;
    
    }
    
    if (message->data_count && message->data[0] == IPC_WRONGTYPE) {
    
        
        request->handler_pid = 0;
        ipc_dispatch_requests ();
        
        if (request->handler_pid) {
            return 0;
        }
    
    }
    
    if (request->wait_reply) {
    
        if (!request->sender || !ipc_deliver (request->sender, request->reply_address, message)) {
            return -EFAULT;
        }
        
        request->sender->ipc_receiving = 0;
        request->sender->ipc_sendrec = 0;
        request->sender->ipc_receive_address = 0;
        
        thread_resume (request->sender);
    
    }
    
    if (request->execve && request->sender) {
    
        if (message->data_count && message->data[0] != IPC_SUCCESSFUL) {
        
            intptr_t error = message->data_count > 1 && message->data[1] ? (intptr_t) message->data[1] : (message->data[0] == IPC_NOTFOUND ? -ENOENT : (message->data[0] == IPC_NOTENOUGHMEMORY ? -ENOMEM : (message->data[0] == IPC_ACCESSDENIED ? -EACCES : -ENOEXEC)));
            dprintf ("[execve] loader failed: %d\n", (int) error);
            
#if     defined (__AMD64__)
            request->sender->regs.rax = (uintptr_t) error;
#else
            request->sender->regs.eax = (uintptr_t) error;
#endif
        
        }
        
        thread_resume (request->sender);
    
    }
    
    if (previous) {
        previous->next = request->next;
    } else {
        ipc_requests = request->next;
    }
    
    while (request->attempts) {
    
        attempt = request->attempts;
        request->attempts = attempt->next;
        
        kfree (attempt);
    
    }
    
    if (request->execve) {
    
        request->message.buffer = 0;
        
        kfree (request->execve->strings);
        kfree (request->execve);
    
    }
    
    ipc_discard_message (&request->message);
    kfree (request);
    
    return 0;

}

/* Queue a message until a process requests its type with ipc_receive. */
static int ipc_queue_request (ipc_message_t *message, struct thread *sender, uintptr_t reply_address, int wait_reply, struct execve_data *execve) {

    struct ipc_request *request, *tail;
    
    if (!(request = kmalloc (sizeof (*request)))) {
        return -ENOMEM;
    }
    
    memset (request, 0, sizeof (*request));
    
    request->message = *message;
    request->sender = sender;
    request->reply_address = reply_address;
    request->wait_reply = wait_reply;
    request->execve = execve;
    
    if (message->type == IPC_MESSAGE_LOAD && message->size && message->buffer) {
        dprintf ("[ipc-queue] size=%lu first=%x buffer=%p\n", (unsigned long) message->size, ((const uint8_t *) message->buffer)[0], message->buffer);
    }
    
    if (!ipc_requests) {
        ipc_requests = request;
    } else {
    
        tail = ipc_requests;
        
        while (tail->next) {
            tail = tail->next;
        }
        
        tail->next = request;
    
    }
    
    ipc_dispatch_requests ();
    return 0;

}

static int execve_copy_string (uintptr_t pd, uintptr_t address, struct execve_data *data, size_t *position) {

    uint8_t character;
    *position = data->bytes;
    
    if (!address) {
    
        dprintf ("[execve] null string address\n");
        return -EFAULT;
    
    }
    
    do {
    
        if (data->bytes == EXECVE_MAX_BYTES) {
            return -E2BIG;
        }
        
        if (!mmu_copy_user (pd, address, &character, 1, 0)) {
        
            dprintf ("[execve] unreadable string address=%p\n", (void *) address);
            return -EFAULT;
        
        }
        
        data->strings[data->bytes++] = character;
        
        if (!character) {
            return 0;
        }
        
        if (address == (uintptr_t) -1) {
            return -EFAULT;
        }
        
        address++;
    
    } while (1);

}

static int execve_copy_vector (uintptr_t pd, uintptr_t address, struct execve_data *data, size_t *positions, size_t *count, int compat32) {

    size_t i;
    
#ifndef     __AMD64__
    (void) compat32;
#endif
    
    *count = 0;
    
    if (!address) {
        return 0;
    }
    
    for (i = 0; i < EXECVE_MAX_STRINGS; i++) {
    
        uintptr_t pointer;
        int result;
        
#if     defined (__AMD64__)
        if (compat32) {
        
            uint32_t pointer32;
            
            if (address > (uintptr_t) -1 - i * sizeof (pointer32) || !mmu_copy_user (pd, address + i * sizeof (pointer32), &pointer32, sizeof (pointer32), 0)) {
                return -EFAULT;
            }
            
            pointer = pointer32;
        
        } else
#endif
        
        if (address > (uintptr_t) -1 - i * sizeof (pointer) || !mmu_copy_user (pd, address + i * sizeof (pointer), &pointer, sizeof (pointer), 0)) {
        
            dprintf ("[execve] unreadable vector address=%p index=%u\n", (void *) address, (unsigned) i);
            return -EFAULT;
        
        }
        
        if (!pointer) {
            return 0;
        }
        
        if ((result = execve_copy_string (pd, pointer, data, &positions[i]))) {
            return result;
        }
        
        (*count)++;
    
    }
    
    return -E2BIG;

}

#include    <stdio.h>

int process_execve (const char *path, char *const argv[], char *const envp[], struct regs *regs) {

    struct thread *sender = thread_get_current ();
    ipc_message_t message = { 0 };
    
    size_t position, path_length;
    char *absolute_path;
    
    struct execve_data *data;
    int result;
    
    if (!sender || !sender->user_mode || !sender->owner || !regs || (regs->cs & 3) != 3) {
        return -EINVAL;
    }
    
    if (!(data = kmalloc (sizeof (*data)))) {
        return -ENOMEM;
    }
    
    memset (data, 0, sizeof (*data));
    
    if (!(data->strings = kmalloc (EXECVE_MAX_BYTES))) {
    
        kfree (data);
        return -ENOMEM;
    
    }
    
    if ((result = execve_copy_string (sender->owner->pd, (uintptr_t) path, data, &position))) {
    
        dprintf ("[execve] path copy failed: %d, path=%p\n", result, (void *) path);
        goto failed;
    
    }
    
    if (!data->strings[0]) {
    
        result = -ENOENT;
        goto failed;
    
    }
    
    if (!(absolute_path = vfs_canonicalize_path (data->strings, &result))) {
        goto failed;
    
    }
    
    path_length = strlen (absolute_path) + 1;
    
    if (path_length > EXECVE_MAX_BYTES) {
    
        kfree (absolute_path);
        
        result = -ENAMETOOLONG;
        goto failed;
    
    }
    
    memcpy (data->strings, absolute_path, path_length);
    data->bytes = path_length;
    
    kfree (absolute_path);
    
    if ((result = execve_copy_vector (sender->owner->pd, (uintptr_t) argv, data, data->argv, &data->argc, sender->regs.cs == 0x1B && sizeof (uintptr_t) == 8))) {
    
        dprintf ("[execve] argv copy failed: %d, argv=%p\n", result, (void *) argv);
        goto failed;
    
    }
    
    if ((result = execve_copy_vector (sender->owner->pd, (uintptr_t) envp, data, data->envp, &data->envc, sender->regs.cs == 0x1B && sizeof (uintptr_t) == 8))) {
    
        dprintf ("[execve] envp copy failed: %d, envp=%p\n", result, (void *) envp);
        goto failed;
    
    }
    
    message.source = sender->owner->pid;
    message.type = IPC_MESSAGE_LOAD;
    
    message.buffer = data->strings;
    message.size = strlen (data->strings) + 1;
    
    if (ipc_queue_request (&message, sender, 0, 0, data)) {
    
        result = -ENOMEM;
        goto failed;
    
    }
    
    sender->state = TS_WAITIO;
    
    ipc_set_result (regs, 0);
    schedule (regs);
    
    return 0;

failed:

    dprintf ("[execve] failed before scheduling: %d\n", result);
    
    kfree (data->strings);
    kfree (data);
    
    return result;

}

static int ipc_send_internal (uint32_t destination, const ipc_message_t *message, struct regs *regs, int want_reply) {

    struct thread *sender = thread_get_current ();
    struct thread *receiver, *tail;
    
    ipc_message_t copy;
    uint32_t source;
    
    int result;
    
    if (!sender || !sender->owner || !message || sender->ipc_sending || sender->ipc_receiving) {
        return -EINVAL;
    }
    
    source = sender->owner->pid;
    
    if (source == destination && destination != IPC_MESSAGE_ANY) {
        return -EDEADLK;
    }
    
    if (want_reply && (!regs || (sender->user_mode && !mmu_user_buffer_valid (sender->owner->pd, (uintptr_t) message, sizeof (copy), 1)))) {
        return -EFAULT;
    }
    
    if ((result = ipc_copy_message (sender, message, &copy))) {
        return result;
    }
    
    if (destination == IPC_MESSAGE_ANY && ((copy.type & IPC_MESSAGE_EXEC_CHIMAERA) || (copy.type & IPC_MESSAGE_EXEC_NT))) {
    
        if (copy.type & IPC_MESSAGE_EXEC_MODULE) {
        
            ipc_buffer_t buffer;
            
            if (copy.type != (IPC_MESSAGE_EXEC_CHIMAERA | IPC_MESSAGE_EXEC_MODULE) || copy.data_count != 1 || copy.buffers_count || !sender->owner->module_size) {
            
                ipc_discard_message (&copy);
                return -EINVAL;
            
            }
            
            buffer.address = sender->owner->module_base;
            buffer.data = (void *) buffer.address;
            buffer.size = sender->owner->module_size;
            
            result = process_exec_loader_start_buffers (&buffer, 1, copy.data[0], copy.type, copy.data, copy.data_count);
            
            ipc_discard_message (&copy);
            return result;
        
        }
        
        if (!copy.data_count || !copy.buffers_count) {
        
            ipc_discard_message (&copy);
            return -EINVAL;
        
        }
        
        result = process_exec_loader_start_buffers (copy.buffers, copy.buffers_count, copy.data[0], copy.type, copy.data, copy.data_count);
        
        ipc_discard_message (&copy);
        return result;
    
    }
    
    if (source != destination) {
    
        result = ipc_reply_to_request (destination, &copy, sender);
        
        if (!result) {
        
            ipc_discard_message (&copy);
            return 0;
        
        }
    
    }
    
    /* IPC_MESSAGE_ANY sends one request to a process waiting for this message type. */
    if (destination == IPC_MESSAGE_ANY) {
    
        result = ipc_queue_request (&copy, sender, (uintptr_t) message, want_reply, 0);
        
        if (result) {
        
            ipc_discard_message (&copy);
            return result;
        
        }
        
        if (!want_reply) {
            return 0;
        }
        
        sender->ipc_receiving = 1;
        
        sender->ipc_receive_from = IPC_MESSAGE_ANY;
        sender->ipc_receive_address = (uintptr_t) message;
        
        sender->ipc_sendrec = 1;
        sender->state = TS_WAITIO;
        
        ipc_set_result (regs, 0);
        
        schedule (regs);
        return 0;
    
    }
    
    if (!(receiver = ipc_find_thread (destination))) {
    
        ipc_discard_message (&copy);
        return -ESRCH;
    
    }
    
    if (receiver->ipc_receiving && !receiver->ipc_sending && (receiver->ipc_receive_from == IPC_MESSAGE_ANY || receiver->ipc_receive_from == source) && (!receiver->ipc_receive_type || receiver->ipc_receive_type == copy.type)) {
    
        if (!ipc_deliver (receiver, receiver->ipc_receive_address, &copy)) {
        
            ipc_discard_message (&copy);
            return -EFAULT;
        
        }
        
        receiver->ipc_receiving = 0;
        receiver->ipc_sendrec = 0;
        
        receiver->ipc_receive_address = 0;
        receiver->ipc_receive_type = 0;
        
        thread_resume (receiver);
        ipc_discard_message (&copy);
        
        if (!want_reply) {
            return 0;
        }
    
    } else {
    
        if (!regs) {
        
            ipc_discard_message (&copy);
            return -1;
        
        }
        
        sender->ipc_outgoing_message = copy;
        sender->ipc_sending = 1;
        sender->ipc_send_to = destination;
        sender->ipc_sender_next = 0;
        
        if (!receiver->ipc_sender_head) {
            receiver->ipc_sender_head = sender;
        } else {
        
            tail = receiver->ipc_sender_head;
            
            while (tail->ipc_sender_next) {
                tail = tail->ipc_sender_next;
            }
            
            tail->ipc_sender_next = sender;
        
        }
    
    }
    
    if (want_reply) {
    
        sender->ipc_receive_address = (uintptr_t) message;
        sender->ipc_receive_from = destination;
        
        sender->ipc_receiving = 1;
        sender->ipc_sendrec = 1;
    
    }
    
    sender->state = TS_WAITIO;
    
    ipc_set_result (regs, 0);
    schedule (regs);
    
    return 0;

}

/* Build the Windows process parameters read by the C runtime. */
static int nt_append_character (uint16_t *text, size_t capacity, size_t *length, unsigned char character) {

    if (*length >= capacity) {
        return 0;
    }
    
    text[(*length)++] = character;
    return 1;

}

static int nt_append_argument (uint16_t *text, size_t capacity, size_t *length, const char *argument) {

    const char *p;
    size_t slashes, j;
    
    int quoted = !*argument;
    
    for (p = argument; *p; p++) {
    
        if (*p == ' ' || *p == '\t' || *p == '"') {
        
            quoted = 1;
            break;
        
        }
    
    }
    
    if (quoted && !nt_append_character (text, capacity, length, '"')) {
        return 0;
    }
    
    p = argument;
    
    while (*p) {
    
        slashes = 0;
        
        while (*p == '\\') {
        
            slashes++;
            p++;
        
        }
        
        if (*p == '"') {
        
            for (j = 0; j < slashes * 2 + 1; j++) {
            
                if (!nt_append_character (text, capacity, length, '\\')) {
                    return 0;
                }
            
            }
            
            if (!nt_append_character (text, capacity, length, '"')) {
                return 0;
            }
            
            p++;
            continue;
        
        }
        
        for (j = 0; j < slashes * (*p ? 1 : (quoted ? 2 : 1)); j++) {
        
            if (!nt_append_character (text, capacity, length, '\\')) {
                return 0;
            }
        
        }
        
        if (*p) {
        
            if (!nt_append_character (text, capacity, length, (unsigned char) *p)) {
                return 0;
            }
            
            p++;
        
        }
    
    }
    
    return !quoted || nt_append_character (text, capacity, length, '"');

}

static int process_map_nt_parameters (uintptr_t pd, const char *path, const struct execve_data *args, uint32_t exec_type) {

    uint8_t *block;
    uint16_t *text;
    
    uintptr_t command_line, image_path, environment;
    size_t length, capacity, pos, cmd_length, image_length, i, j, page_size;
    
    int is32 = 1;
    
    if (!path) {
        return 0;
    }
    
#if     defined (__AMD64__)
    is32 = (exec_type & IPC_MESSAGE_EXEC_32BIT) == IPC_MESSAGE_EXEC_32BIT;
#else
    (void) exec_type;
#endif
    
    capacity = 0x100 + (strlen (path) + 1) * 4 + (args ? args->bytes * 4 : 0) + 64;
    
    if (capacity > NT_TEB_ADDRESS - NT_PARAMETERS_ADDRESS) {
        return 0;
    }
    
    if (!(block = kmalloc (capacity))) {
        return 0;
    }
    
    memset (block, 0, capacity);
    
    text = (uint16_t *) (block + 0x100);
    length = (capacity - 0x100) / sizeof (*text);
    
    pos = 0;
    
    if (args && args->argc) {
    
        for (i = 0; i < args->argc; i++) {
        
            if (i && !nt_append_character (text, length, &pos, ' ')) {
                goto failed;
            }
            
            if (!nt_append_argument (text, length, &pos, args->strings + args->argv[i])) {
                goto failed;
            }
        
        }
    
    } else if (!nt_append_argument (text, length, &pos, path)) {
        goto failed;
    }
    
    cmd_length = pos * sizeof (*text);
    
    if (cmd_length > 0xFFFD || !nt_append_character (text, length, &pos, 0)) {
        goto failed;
    }
    
    command_line = NT_PARAMETERS_ADDRESS + 0x100;
    image_path = NT_PARAMETERS_ADDRESS + 0x100 + pos * sizeof (*text);
    
    for (i = 0; path[i]; i++) {
    
        if (!nt_append_character (text, length, &pos, (unsigned char) path[i])) {
            goto failed;
        }
    
    }
    
    image_length = strlen (path) * sizeof (*text);
    
    if (image_length > 0xFFFD || !nt_append_character (text, length, &pos, 0)) {
        goto failed;
    }
    
    environment = NT_PARAMETERS_ADDRESS + 0x100 + pos * sizeof (*text);
    
    if (args) {
    
        for (i = 0; i < args->envc; i++) {
        
            const char *value = args->strings + args->envp[i];
            
            for (j = 0; value[j]; j++) {
            
                if (!nt_append_character (text, length, &pos, (unsigned char) value[j])) {
                    goto failed;
                }
            
            }
            
            if (!nt_append_character (text, length, &pos, 0)) {
                goto failed;
            }
        
        }
    
    }
    
    if (!nt_append_character (text, length, &pos, 0)) {
        goto failed;
    }
    
    if ((!args || !args->envc) && !nt_append_character (text, length, &pos, 0)) {
        goto failed;
    }
    
    if (is32) {
    
        *(uint32_t *) (block + 0x00) = 0x100;
        *(uint32_t *) (block + 0x04) = 0x100;
        
        *(uint16_t *) (block + 0x38) = (uint16_t) image_length;
        *(uint16_t *) (block + 0x3A) = (uint16_t) (image_length + 2);
        *(uint32_t *) (block + 0x3C) = (uint32_t) image_path;
        
        *(uint16_t *) (block + 0x40) = (uint16_t) cmd_length;
        *(uint16_t *) (block + 0x42) = (uint16_t) (cmd_length + 2);
        *(uint32_t *) (block + 0x44) = (uint32_t) command_line;
        *(uint32_t *) (block + 0x48) = (uint32_t) environment;
    
    } else {
    
        *(uint32_t *) (block + 0x00) = 0x100;
        *(uint32_t *) (block + 0x04) = 0x100;
        
        *(uint16_t *) (block + 0x60) = (uint16_t) image_length;
        *(uint16_t *) (block + 0x62) = (uint16_t) (image_length + 2);
        *(uintptr_t *) (block + 0x68) = image_path;
        
        *(uint16_t *) (block + 0x70) = (uint16_t) cmd_length;
        *(uint16_t *) (block + 0x72) = (uint16_t) (cmd_length + 2);
        *(uintptr_t *) (block + 0x78) = command_line;
        *(uintptr_t *) (block + 0x80) = environment;
    
    }
    
    capacity = 0x100 + pos * sizeof (*text);
    
    for (i = 0; i < capacity; i += PAGE_SIZE) {
    
        page_size = capacity - i;
        
        if (page_size > PAGE_SIZE) {
            page_size = PAGE_SIZE;
        }
        
        if (!mmu_install_user_page (pd, NT_PARAMETERS_ADDRESS + i, block + i, page_size)) {
            goto failed;
        }
    
    }
    
    kfree (block);
    return 1;

failed:

    kfree (block);
    return 0;

}

static int process_map_nt_environment (uintptr_t pd, uint32_t pid, uint32_t tid, uintptr_t image_base, uintptr_t stack_top, uintptr_t stack_bottom, uint32_t exec_type, const char *path, const struct execve_data *args) {

    if (!process_map_nt_parameters (pd, path, args, exec_type)) {
        return 0;
    }

#if     defined (__AMD64__)
    if (!(exec_type & IPC_MESSAGE_EXEC_32BIT)) {
    
        uintptr_t teb[16] = { 0 };
        uintptr_t peb[8] = { 0 };
        
        teb[0] = (uintptr_t) -1;       /* NT_TIB.ExceptionList */
        teb[1] = stack_top;            /* NT_TIB.StackBase */
        teb[2] = stack_bottom;         /* NT_TIB.StackLimit */
        teb[6] = NT_TEB_ADDRESS;       /* NT_TIB.Self, GS:[0x30] */
        teb[8] = pid;                  /* ClientId.UniqueProcess */
        teb[9] = tid;                  /* ClientId.UniqueThread */
        teb[12] = NT_PEB_ADDRESS;      /* ProcessEnvironmentBlock, GS:[0x60] */
        
        peb[2] = image_base;                                /* PEB.ImageBaseAddress */
        peb[4] = NT_PARAMETERS_ADDRESS;                     /* PEB.ProcessParameters */
        
        if (!mmu_install_user_page (pd, NT_PEB_ADDRESS, peb, sizeof (peb))) {
            return 0;
        }
        
        return mmu_install_user_page (pd, NT_TEB_ADDRESS, teb, sizeof (teb));
    
    }
#else
    (void) exec_type;
#endif
    
    {
    
        uint32_t teb[16] = { 0 };
        uint32_t peb[8] = { 0 };
        
        teb[0] = 0xFFFFFFFFU;           /* NT_TIB.ExceptionList, FS:[0] */
        teb[1] = stack_top;             /* NT_TIB.StackBase */
        teb[2] = stack_bottom;          /* NT_TIB.StackLimit */
        teb[6] = NT_TEB_ADDRESS;        /* NT_TIB.Self, FS:[0x18] */
        teb[8] = pid;                   /* ClientId.UniqueProcess */
        teb[9] = tid;                   /* ClientId.UniqueThread */
        teb[12] = NT_PEB_ADDRESS;       /* ProcessEnvironmentBlock, FS:[0x30] */
        
        peb[2] = image_base;                                /* PEB.ImageBaseAddress */
        peb[4] = NT_PARAMETERS_ADDRESS;                     /* PEB.ProcessParameters */
        
        if (!mmu_install_user_page (pd, NT_PEB_ADDRESS, peb, sizeof (peb))) {
            return 0;
        }
        
        return mmu_install_user_page (pd, NT_TEB_ADDRESS, teb, sizeof (teb));
    
    }

}

static void process_configure_nt_thread (struct thread *thread, uint32_t exec_type) {

#if     defined (__AMD64__)
    if (exec_type & IPC_MESSAGE_EXEC_32BIT) {
        thread->tls_base = NT_TEB_ADDRESS;
    } else {
        thread->nt_teb_base = NT_TEB_ADDRESS;
    }
#else
    (void) exec_type;
    
    thread->regs.fs = 0x33;
    thread->tls_base = NT_TEB_ADDRESS;
    thread->tls_limit = PAGE_SIZE - 1;
    thread->tls_access = 0xF2;
    thread->tls_flags = 0x40;
#endif

}

int ipc_send (uint32_t destination, const ipc_message_t *message, struct regs *regs) {
    return ipc_send_internal (destination, message, regs, 0);
}

int ipc_sendrec (uint32_t destination, ipc_message_t *message, struct regs *regs) {
    return ipc_send_internal (destination, message, regs, 1);
}

int ipc_receive (uint32_t source, ipc_message_t *message, struct regs *regs) {

    struct thread *receiver = thread_get_current ();
    struct thread *sender, *previous;
    
    uintptr_t address = (uintptr_t) message;
    
    ipc_message_t requested;
    uint32_t type = 0;

    if (!receiver || !receiver->owner || !message || receiver->ipc_sending || receiver->ipc_receiving) {
        return -EINVAL;
    }
    
    if (source != IPC_MESSAGE_ANY && !ipc_find_thread (source)) {
        return -ESRCH;
    }
    
    if (receiver->user_mode) {
    
        if (!mmu_copy_user (receiver->owner->pd, address, &requested, sizeof (requested), 0) || !mmu_user_buffer_valid (receiver->owner->pd, address, sizeof (*message), 1)) {
            return -EFAULT;
        }
    
    } else {
        memcpy (&requested, message, sizeof (requested));
    }
    
    if (source == IPC_MESSAGE_ANY) {
        type = requested.type;
    }
    
    previous = 0;
    sender = receiver->ipc_sender_head;
    
    while (sender) {
    
        if ((source == IPC_MESSAGE_ANY || sender->owner->pid == source) && (!type || sender->ipc_outgoing_message.type == type)) {
            break;
        }
        
        previous = sender;
        sender = sender->ipc_sender_next;
    
    }
    
    if (sender) {
    
        if (!ipc_deliver (receiver, address, &sender->ipc_outgoing_message)) {
            return -EFAULT;
        }
        
        if (previous) {
            previous->ipc_sender_next = sender->ipc_sender_next;
        } else {
            receiver->ipc_sender_head = sender->ipc_sender_next;
        }
        
        sender->ipc_sender_next = 0;
        sender->ipc_sending = 0;
        
        ipc_discard_message (&sender->ipc_outgoing_message);
        
        if (!sender->ipc_sendrec) {
            thread_resume (sender);
        }
        
        return 0;
    
    }
    
    if (!regs) {
        return -EFAULT;
    }
    
    receiver->ipc_receive_address = address;
    receiver->ipc_receiving = 1;
    
    receiver->ipc_receive_from = source;
    receiver->ipc_receive_type = type;
    
    receiver->state = TS_WAITIO;
    
    ipc_set_result (regs, 0);
    ipc_dispatch_requests ();
    
    if (receiver->ipc_receiving) {
        schedule (regs);
    }
    
    return 0;

}

int process_add_file (struct process *process, struct fs_node *node) {

    int i;
    
    for (i = 0; i < 256; i++) {
    
        if (!process->fd[i]) {
            break;
        }
    
    }
    
    if (i == 256) {
        return -EMFILE;
    }
    
    process->dir_index[i] = 0;
    process->fd[i] = node;
    
    return i;

}

int process_close_file (struct process *process, int __fd) {

    void *node;
    
    if (__fd < 0 || __fd >= 256) {
        return -EBADF;
    }
    
    if (!(node = process->fd[__fd])) {
        return -EBADF;
    }
    
    vfs_close (node);
    
    process->dir_index[__fd] = 0;
    process->fd[__fd] = 0;
    
    return 0;

}

struct fs_node *process_get_file (struct process *process, int __fd) {

    void *node;
    
    if (__fd < 0 || __fd >= 256) {
        return -1;
    }
    
    if (!(node = process->fd[__fd])) {
        return -1;
    }
    
    return node;

}

/******************************************************************************
 * @function        process_create_user_process_from_file
 *****************************************************************************/
int process_create_user_process_from_file (const char *path) {

    struct thread *sender = thread_get_current ();
    ipc_message_t message = { 0 }, copy, reply = { 0 };
    
    uintptr_t reply_data[2] = { 0 };
    size_t size;
    
    int result;
    
    if (!sender || !sender->owner || sender->user_mode || !path || path[0] != '/') {
        return -EINVAL;
    }
    
    size = strlen (path) + 1;
    
    if (size < 2) {
        return -ENOENT;
    }
    
    message.buffer = (void *) path;
    message.size = size;
    
    message.type = IPC_MESSAGE_LOAD;
    
    if ((result = ipc_copy_message (sender, &message, &copy))) {
        return result;
    }
    
    reply.data = reply_data;
    reply.data_capacity = 2;
    
    sender->state = TS_WAITIO;
    
    if (ipc_queue_request (&copy, sender, (uintptr_t) &reply, 1, 0)) {
    
        thread_resume (sender);
        
        ipc_discard_message (&copy);
        return -ENOMEM;
    
    }
    
    while (sender->state == TS_WAITIO) {
    
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("hlt");
#endif
    
    }
    
    return reply.data_count && reply_data[0] == IPC_SUCCESSFUL ? 0 : -ENOEXEC;

}

static void process_copy_module (void *destination, const void *source, size_t size) {

    uintptr_t flags, pd;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("pushfq");
#else
    __asm__ __volatile__ ("pushfd");
#endif
    
    __asm__ __volatile__ ("pop %0" : "=r" (flags) : : "memory");
    __asm__ __volatile__ ("cli");
    
    __asm__ __volatile__ ("mov %0, cr3" : "=r" (pd));
    __asm__ __volatile__ ("mov cr3, %0" : : "r" (kernel_page_directory_physical) : "memory");
    
    memcpy (destination, source, size);
    
    __asm__ __volatile__ ("mov cr3, %0" : : "r" (pd) : "memory");
    __asm__ __volatile__ ("push %0" : : "r" (flags) : "memory");
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("popfq");
#else
    __asm__ __volatile__ ("popfd");
#endif

}

/* Map each executable and shared-library image into the new address space. */
static int process_map_ipc_buffers (uintptr_t pd, const ipc_buffer_t *buffers, size_t count, uintptr_t stack_bottom, uint32_t exec_type) {

    uintptr_t base, end, previous_base, previous_end;
    size_t i, j, position, chunk, offset, total, span;
    
    uint8_t *page = 0;
    
    if (!buffers || !count) {
        return -ENOEXEC;
    }
    
    for (i = 0; i < count; i++) {
    
        if (!buffers[i].data || !buffers[i].size || !buffers[i].address || buffers[i].size > 0x30000000) {
            return -ENOEXEC;
        }
        
        offset = buffers[i].address & (PAGE_SIZE - 1);
        base = buffers[i].address - offset;
        
        if (buffers[i].size > 0x30000000 - offset) {
            return -ENOEXEC;
        }
        
        total = buffers[i].size + offset;
        span = (total + PAGE_SIZE - 1) & ~((size_t) PAGE_SIZE - 1);
        
        if (base > (uintptr_t) -1 - span) {
            return -ENOEXEC;
        }
        
        end = base + span;
        
#if     defined (__AMD64__)
        if ((exec_type & IPC_MESSAGE_EXEC_32BIT) && (end > 0x100000000ULL || (base < NT_PEB_ADDRESS + PAGE_SIZE && end > NT_PARAMETERS_ADDRESS) || (base < EXECVE_STACK_TOP && end > stack_bottom))) {
            return -ENOEXEC;
        }
#else
        (void) exec_type;
#endif

#if     defined (__AMD64__)
        if (!(exec_type & IPC_MESSAGE_EXEC_32BIT) && ((base >= stack_bottom && base < 0x4040000000ULL) ||
            (base < stack_bottom && span > stack_bottom - base) ||
            (base >= 0x4040000000ULL && end > 0x0000800000000000ULL))) {
#else
        if ((base >= stack_bottom && base < 0xC0000000UL) ||
            (base < stack_bottom && span > stack_bottom - base)) {
#endif
            return -ENOEXEC;
        }
        
        for (j = 0; j < i; j++) {
        
            previous_base = buffers[j].address & ~((uintptr_t) PAGE_SIZE - 1);
            previous_end = previous_base + ((buffers[j].size + (buffers[j].address & (PAGE_SIZE - 1)) + PAGE_SIZE - 1) & ~((size_t) PAGE_SIZE - 1));
            
            if (base < previous_end && previous_base < end) {
                return -ENOEXEC;
            }
        
        }
    
    }
    
    for (i = 0; i < count; i++) {
    
        base = buffers[i].address & ~((uintptr_t) PAGE_SIZE - 1);
        offset = buffers[i].address - base;
        
        for (position = 0; position < buffers[i].size; position += chunk, base += PAGE_SIZE) {
        
            const uint8_t *source = (const uint8_t *) buffers[i].data + position;
            size_t length;
            
            chunk = buffers[i].size - position;
            length = PAGE_SIZE - offset;
            
            if (chunk > length) {
                chunk = length;
            }
            
            if (offset || chunk < PAGE_SIZE || (exec_type & IPC_MESSAGE_EXEC_MODULE)) {
            
                if (!page && !(page = kmalloc (PAGE_SIZE))) {
                    return -ENOMEM;
                }
                
                memset (page, 0, PAGE_SIZE);
                
                if (exec_type & IPC_MESSAGE_EXEC_MODULE) {
                    process_copy_module (page + offset, source, chunk);
                } else {
                    memcpy (page + offset, source, chunk);
                }
                
                source = page;
            
            }
            
            if (!mmu_install_user_page (pd, base, source, offset || chunk < PAGE_SIZE || (exec_type & IPC_MESSAGE_EXEC_MODULE) ? PAGE_SIZE : chunk)) {
            
                if (page) {
                    kfree (page);
                }
                
                return -ENOMEM;
            
            }
            
            offset = 0;
        
        }
    
    }
    
    if (page) {
        kfree (page);
    }
    
    return 0;

}

static int execve_stack_write (uintptr_t pd, uintptr_t *sp, const void *contents, size_t size) {

    if (*sp < EXECVE_STACK_BOTTOM || size > *sp - EXECVE_STACK_BOTTOM) {
        return 0;
    }
    
    *sp -= size;
    return mmu_copy_user (pd, *sp, (void *) contents, size, 1);

}

static int execve_stack_write_word (uintptr_t pd, uintptr_t address, uintptr_t value, size_t word_size) {

#if     !defined (__AMD64__)
    (void) word_size;
#endif
    
#if     defined (__AMD64__)
    if (word_size == sizeof (uint32_t)) {
    
        uint32_t value32 = (uint32_t) value;
        return mmu_copy_user (pd, address, &value32, sizeof (value32), 1);
    
    }
#endif
    
    return mmu_copy_user (pd, address, &value, sizeof (value), 1);

}

static int process_prepare_dll_entry (uintptr_t pd, const uintptr_t *data, size_t data_count, uintptr_t *entry_point, uintptr_t *stack_pointer, uintptr_t stack_bottom, uint32_t exec_type) {

    uintptr_t next, sp;
    size_t i;
    
    if (data_count <= 1) {
        return 1;
    }
    
#if     defined (__AMD64__)
    if (!(exec_type & IPC_MESSAGE_EXEC_32BIT)) {
        return 1;
    }
#else
    if (!(exec_type & IPC_MESSAGE_EXEC_NT)) {
        return 1;
    }
#endif
    
    if (!data || !(data_count & 1) || (data_count - 1) / 2 > (*stack_pointer - stack_bottom) / 16) {
        return 0;
    }
    
    next = *entry_point;
    sp = *stack_pointer;
    
    for (i = data_count; i > 1; i -= 2) {
    
        uint32_t frame[4];
        
        if (data[i - 2] > 0xFFFFFFFFULL || data[i - 1] > 0xFFFFFFFFULL || !data[i - 1]) {
            return 0;
        }
        
        frame[0] = (uint32_t) next;
        frame[1] = (uint32_t) data[i - 2];
        
        frame[2] = 1;           /* DLL_PROCESS_ATTACH */
        frame[3] = 1;           /* Static load */
        
        sp -= sizeof (frame);
        
        if (!mmu_copy_user (pd, sp, frame, sizeof (frame), 1)) {
            return 0;
        }
        
        next = data[i - 1];
    
    }
    
    *entry_point = next;
    *stack_pointer = sp;
    
    return 1;

}

static int process_execve_replace (struct ipc_request *request, const ipc_buffer_t *buffers, size_t count, uintptr_t entry_point, uint32_t exec_type, const uintptr_t *dll_data, size_t dll_data_count) {

    struct thread *thread = request->sender;
    
    struct process *process;
    struct execve_data *data = request->execve;
    
    uintptr_t pd, old_pd, sp, argv[EXECVE_MAX_STRINGS], envp[EXECVE_MAX_STRINGS];
    uintptr_t value, start;
    
    size_t argc, i, length, words, word_size = sizeof (uintptr_t);
    
    const char *arg, *name, *p;
    int mapped;
    
    if (!thread || !thread->owner || !data || !buffers || !count || !buffers[0].data || !buffers[0].size ||
        entry_point < buffers[0].address || entry_point - buffers[0].address >= buffers[0].size) {
        return -ENOEXEC;
    }
    
#if     defined (__AMD64__)
    if (exec_type & IPC_MESSAGE_EXEC_32BIT) {
    
        if (entry_point > 0xFFFFFFFFULL) {
            return -ENOEXEC;
        }
        
        word_size = sizeof (uint32_t);
    
    }
#endif
    
    if (!(pd = mmu_clone_address_space (kernel_page_directory_physical))) {
        return -ENOMEM;
    }
    
    if ((mapped = process_map_ipc_buffers (pd, buffers, count, EXECVE_STACK_BOTTOM, exec_type))) {
    
        mmu_destroy_address_space (pd);
        return mapped;
    
    }
    
    for (i = EXECVE_STACK_BOTTOM; i < EXECVE_STACK_TOP; i += PAGE_SIZE) {
    
        if (!mmu_install_user_page (pd, i, 0, 0)) {
            goto failed;
        }
    
    }
    
    argc = data->argc;
    
    if ((exec_type & IPC_MESSAGE_EXEC_MODULE) && !argc) {
        argc = 1;
    }
    
    sp = EXECVE_STACK_TOP;
    
    for (i = data->envc; i; i--) {
    
        length = strlen (data->strings + data->envp[i - 1]) + 1;
        
        if (!execve_stack_write (pd, &sp, data->strings + data->envp[i - 1], length)) {
            goto failed;
        }
        
        envp[i - 1] = sp;
    
    }
    
    for (i = argc; i; i--) {
    
        arg = (exec_type & IPC_MESSAGE_EXEC_MODULE) && i == 1 ? (const char *) request->message.buffer : data->strings + data->argv[i - 1];
        length = strlen (arg) + 1;
        
        if (!execve_stack_write (pd, &sp, arg, length)) {
            goto failed;
        }
        
        argv[i - 1] = sp;
    
    }
    
    sp &= ~((uintptr_t) 15);
    
    if ((words = 1 + argc + 1 + data->envc + 1 + 2) > (sp - EXECVE_STACK_BOTTOM) / word_size) {
        goto failed;
    }
    
    start = (sp - words * word_size) & ~((uintptr_t) 15);
    
    if (start < EXECVE_STACK_BOTTOM) {
        goto failed;
    }
    
    sp = start;
    value = argc;
    
    if (!execve_stack_write_word (pd, sp, value, word_size)) {
        goto failed;
    }
    
    sp += word_size;
    
    for (i = 0; i < argc; i++, sp += word_size) {
    
        if (!execve_stack_write_word (pd, sp, argv[i], word_size)) {
            goto failed;
        }
    
    }
    
    value = 0;
    
    if (!execve_stack_write_word (pd, sp, value, word_size)) {
        goto failed;
    }
    
    sp += word_size;
    
    for (i = 0; i < data->envc; i++, sp += word_size) {
    
        if (!execve_stack_write_word (pd, sp, envp[i], word_size)) {
            goto failed;
        }
    
    }
    
    value = 0;
    
    for (i = 0; i < 3; i++, sp += word_size) {
    
        if (!execve_stack_write_word (pd, sp, value, word_size)) {
            goto failed;
        }
    
    }
    
    process = thread->owner;
    
    if ((exec_type & IPC_MESSAGE_EXEC_NT) && !process_map_nt_environment (pd, process->pid, thread->threadId, buffers[0].address, EXECVE_STACK_TOP, EXECVE_STACK_BOTTOM, exec_type, (const char *) request->message.buffer, data)) {
        goto failed;
    }
    
    if (!process_prepare_dll_entry (pd, dll_data, dll_data_count, &entry_point, &start, EXECVE_STACK_BOTTOM, exec_type)) {
        goto failed;
    }
    
    old_pd = process->pd;
    process->pd = pd;
    
    process->module_base = 0;
    process->module_size = 0;
    
    process->brk_begin = 0;
    process->brk_end = 0;
    process->brk_next_unallocated_page_begin = 0;
    
    name = request->message.buffer;
    
    for (p = name; *p; p++) {
    
        if (*p == '/') {
            name = p + 1;
        }
    
    }
    
    for (i = 0; i < PROCESS_NAME_MAX - 1 && name[i]; i++) {
        process->name[i] = name[i];
    }
    
    memset (&thread->regs, 0, sizeof (thread->regs));
    process->name[i] = 0;
    
    thread->regs.cr3 = pd;
    
#if     defined (__AMD64__)
    thread->regs.cs = (exec_type & IPC_MESSAGE_EXEC_32BIT) ? 0x1B : 0x2B;
    thread->regs.rip = entry_point;
    thread->regs.rsp = start;
    thread->regs.rflags = 0x202;
#else
    thread->regs.cs = 0x1B;
    thread->regs.eip = entry_point;
    thread->regs.esp = start;
    thread->regs.eflags = 0x202;
#endif
    
    thread->regs.ss = 0x23;
    thread->regs.ds = 0x23;
    thread->regs.es = 0x23;
    thread->regs.fs = 0x23;
    thread->regs.gs = 0x23;
    
    thread->tls_base = 0;
    
#if     defined (__AMD64__)
    thread->nt_teb_base = 0;
#else
    thread->tls_limit = 0xFFFFFFFF;
    thread->tls_access = 0xF2;
    thread->tls_flags = 0x0C;
#endif
    
    if (exec_type & IPC_MESSAGE_EXEC_NT) {
        process_configure_nt_thread (thread, exec_type);
    }
    
    thread->pending_signal_count = 0;
    
    mmu_destroy_address_space (old_pd);
    return 0;

failed:

    mmu_destroy_address_space (pd);
    return -ENOMEM;

}

static int process_exec_loader_start_buffers (const ipc_buffer_t *buffers, size_t count, uintptr_t entry_point, uint32_t exec_type, const uintptr_t *data, size_t data_count) {

    struct thread *thread = thread_get_current ();
    struct ipc_request *request;
    
    if (!thread || !thread->user_mode || !thread->owner || !buffers || !count || !buffers[0].data ||
        !buffers[0].size || entry_point < buffers[0].address || entry_point - buffers[0].address >= buffers[0].size) {
        return -EINVAL;
    }
    
    for (request = ipc_requests; request; request = request->next) {
    
        if (request->handler_pid == thread->owner->pid && request->message.type == IPC_MESSAGE_LOAD) {
        
            uintptr_t reply_data[2] = { 0 };
            
            ipc_message_t reply = { 0 };
            int result, pid;
            
            reply.data = reply_data;
            reply.data_capacity = 2;
            reply.data_count = 2;
            
            if (request->execve) {
            
                result = process_execve_replace (request, buffers, count, entry_point, exec_type, data, data_count);
                reply.type = IPC_MESSAGE_LOAD;
                
                reply.data[0] = result == -ENOEXEC ? IPC_WRONGTYPE : (result ? IPC_NOTENOUGHMEMORY : IPC_SUCCESSFUL);
                reply.data[1] = (uintptr_t) result;
                
                ipc_reply_to_request (request->message.source, &reply, thread);
                return result;
            
            }
            
            if (exec_type & IPC_MESSAGE_EXEC_MODULE) {
            
                reply.type = IPC_MESSAGE_LOAD;
                reply.data[0] = IPC_WRONGTYPE;
                reply.data[1] = (uintptr_t) -ENOEXEC;
                
                ipc_reply_to_request (request->message.source, &reply, thread);
                return -ENOEXEC;
            
            }
            
            pid = process_create_user_task_at_buffers (buffers, count, entry_point, exec_type, data, data_count, (const char *) request->message.buffer);
            reply.type = IPC_MESSAGE_LOAD;
            
            reply.data[0] = pid < 0 ? IPC_NOTENOUGHMEMORY : IPC_SUCCESSFUL;
            reply.data[1] = (uintptr_t) pid;
            
            if (pid < 0) {
                dprintf ("[init] loader failed creating process: %d\n", pid);
            }
            
            ipc_reply_to_request (request->message.source, &reply, thread);
            return pid;
        
        }
    
    }
    
    return -ESRCH;

}

int process_exec_loader_start (const void *code, size_t size, uintptr_t virtual_base, uintptr_t entry_point) {

    ipc_buffer_t buffer;
    
    buffer.data = (void *) code;
    buffer.size = size;
    buffer.address = virtual_base;
    
    return process_exec_loader_start_buffers (&buffer, 1, entry_point, IPC_MESSAGE_EXEC_CHIMAERA, 0, 0);

}

/******************************************************************************
 * @function        process_create_user_task
 *****************************************************************************/
int process_create_user_task (const void *code, size_t size, uintptr_t virtual_base) {

    ipc_buffer_t buffer;
    
    buffer.data = (void *) code;
    buffer.size = size;
    buffer.address = virtual_base;
    
    return process_create_user_task_at_buffers (&buffer, 1, virtual_base, IPC_MESSAGE_EXEC_CHIMAERA | IPC_MESSAGE_EXEC_MODULE, 0, 0, 0);

}

int process_create_user_task_at (const void *code, size_t size, uintptr_t virtual_base, uintptr_t entry_point) {

    ipc_buffer_t buffer;
    
    buffer.data = (void *) code;
    buffer.size = size;
    buffer.address = virtual_base;
    
    return process_create_user_task_at_buffers (&buffer, 1, entry_point, IPC_MESSAGE_EXEC_CHIMAERA, 0, 0, 0);

}

static int process_create_user_task_at_buffers (const ipc_buffer_t *buffers, size_t count, uintptr_t entry_point, uint32_t exec_type, const uintptr_t *data, size_t data_count, const char *path) {

    struct process *process;
    struct thread *thread, *p;
    
    uint8_t *stack;
    int mapped, i;
    
    uintptr_t pd, virtual_base, start = 0x70000000 - 4 * sizeof (uintptr_t);
    
    if (!buffers || !count || !buffers[0].data || !buffers[0].size || !buffers[0].address || !g_current_thread ||
        entry_point < buffers[0].address || entry_point - buffers[0].address >= buffers[0].size) {
    
        dprintf ("[module] invalid arguments: %p\n", (void *) (buffers && count ? buffers[0].address : 0));
        return -EINVAL;
    
    }
    
    virtual_base = buffers[0].address;
    dprintf ("[module-create] before clone: base=%p size=%lx\n", (void *) virtual_base, (unsigned long) buffers[0].size);
    
    if (!(pd = mmu_clone_address_space (kernel_page_directory_physical))) {
    
        dprintf ("[module] failed cloning address space\n");
        return -ENOMEM;
    
    }
    
    dprintf ("[module-create] after clone: base=%p pd=%p\n", (void *) virtual_base, (void *) pd);
    
    if ((mapped = process_map_ipc_buffers (pd, buffers, count, 0x6FFFF000, exec_type & ~IPC_MESSAGE_EXEC_MODULE))) {
    
        dprintf ("[module] failed mapping image: %d\n", mapped);
        
        mmu_destroy_address_space (pd);
        return mapped;
    
    }
    
    dprintf ("[module-create] image mapped: base=%p\n", (void *) virtual_base);
    
    if (!mmu_install_user_page (pd, 0x6FFFF000, 0, 0)) {
    
        dprintf ("[module] failed mapping stack page\n");
        
        mmu_destroy_address_space (pd);
        return -ENOMEM;
    
    }
    
    dprintf ("[module-create] stack mapped: base=%p\n", (void *) virtual_base);
    
    if (!process_prepare_dll_entry (pd, data, data_count, &entry_point, &start, 0x6FFFF000, exec_type)) {
    
        mmu_destroy_address_space (pd);
        return -ENOMEM;
    
    }
    
    if (!(process = kmalloc (sizeof (*process)))) {
    
        dprintf ("[module] failed allocating process\n");
        
        mmu_destroy_address_space (pd);
        return -ENOMEM;
    
    }
    
    if (!(thread = kmalloc (sizeof (*thread)))) {
    
        dprintf ("[module] failed allocating thread\n");
        kfree (process);
        
        mmu_destroy_address_space (pd);
        return -ENOMEM;
    
    }
    
    if (!(stack = kmalloc (0x8000))) {
    
        dprintf ("[module] failed allocating kernel stack\n");
        
        kfree (thread);
        kfree (process);
        
        mmu_destroy_address_space (pd);
        return -ENOMEM;
    
    }
    
    dprintf ("[module-create] allocations complete: base=%p\n", (void *) virtual_base);
    
    memset (process, 0, sizeof (*process));
    memset (thread, 0, sizeof (*thread));
    memset (stack, 0, 0x8000);
    
    if (g_current_thread && g_current_thread->owner) {
        process->console = g_current_thread->owner->console;
    }
    
    strcpy (process->cwd, g_current_thread->owner->cwd);
    strcpy (process->name, "[init]");
    
    process->pid = generate_process_id ();
    process->pd = pd;
    process->parent = kernel_process;
    
    if (!path && (exec_type & IPC_MESSAGE_EXEC_MODULE)) {
    
        process->module_base = virtual_base;
        process->module_size = buffers[0].size;
    
    }
    
    thread->owner = process;
    
    thread->threadId = generate_thread_id ();
    thread->user_mode = 1;
    
    thread->birth_time = get_uptime_milliseconds ();
    thread->regs.cr3 = pd;
    
#if     defined (__AMD64__)
    thread->regs.cs = (exec_type & IPC_MESSAGE_EXEC_32BIT) ? 0x1B : 0x2B;
#else
    thread->regs.cs = 0x1B;
#endif
    
    thread->regs.ss = 0x23;
    thread->regs.ds = 0x23;
    thread->regs.es = 0x23;
    thread->regs.fs = 0x23;
    thread->regs.gs = 0x23;
    
    thread->kstack.stack_start = (uintptr_t) stack;
    
#if     defined (__AMD64__)
    thread->regs.rip = entry_point;
    thread->regs.rsp = start;
    
    thread->regs.rflags = 0x202;
    thread->kstack.rsp0 = (uintptr_t) (stack + 0x8000);
#else
    thread->regs.eip = entry_point;
    thread->regs.esp = start;
    
    thread->regs.eflags = 0x202;
    
    thread->kstack.esp0 = (uintptr_t) (stack + 0x8000);
    thread->kstack.ss0 = 0x10;
    
    thread->tls_limit = 0xFFFFFFFF;
    
    thread->tls_access = 0xF2;
    thread->tls_flags = 0x0C;
#endif
    
    if (exec_type & IPC_MESSAGE_EXEC_NT) {
    
        if (!process_map_nt_environment (pd, process->pid, thread->threadId, buffers[0].address, 0x70000000UL, 0x6FFFF000UL, exec_type, path, 0)) {
        
            kfree (process);
            kfree (thread);
            kfree (stack);
            
            mmu_destroy_address_space (pd);
            return -ENOMEM;
        
        }
        
        process_configure_nt_thread (thread, exec_type);
    
    }
    
    thread_resume (thread);
    
    /* File descriptors are inherited; they must not be reopened. */
    for (i = 0; i < 256; i++) {
    
        if (process->parent->fd[i]) {
        
            process->fd[i] = process->parent->fd[i];
            vfs_retain (process->fd[i]);
        
        }
    
    }
    
    p = g_current_thread;
    
    while (p->next) {
        p = (struct thread *) p->next;
    }
    
    dprintf ("[module-create] linked: base=%p pid=%u\n", (void *) virtual_base, (unsigned) process->pid);
    p->next = thread;
    
    return (int) process->pid;

}

int process_fork_from_clone (struct regs *frame, uintptr_t cloned_pd) {

    struct thread *parent_thread = g_current_thread;
    
    struct process *parent, *process;
    struct thread *child_thread, *p;
    
    uint8_t *stack;
    int i;
    
    if (!frame || !cloned_pd || !parent_thread || !parent_thread->user_mode || (frame->cs & 3) != 3) {
        return -EINVAL;
    }
    
    parent = parent_thread->owner;
    
    if (!parent) {
        return -ESRCH;
    }
    
    if (!(process = kmalloc (sizeof (*process)))) {
        return -ENOMEM;
    }
    
    if (!(child_thread = kmalloc (sizeof (*child_thread)))) {
    
        kfree (process);
        return -ENOMEM;
    
    }
    
    if (!(stack = kmalloc (0x8000))) {
    
        kfree (child_thread);
        kfree (process);
        
        return -ENOMEM;
    
    }
    
    memcpy (process, parent, sizeof (*process));
    
    process->console = parent->console;
    process->pid = generate_process_id ();
    process->pd = cloned_pd;
    process->parent = parent;
    process->exiting = 0;
    process->reaped = 0;
    process->exit_status = 0;
    process->module_base = 0;
    process->module_size = 0;
    
    /* The child's process-local IPC root must be initialized separately. */
    process->ipc_node = 0;
    
    /* Make sure the child thread is zeroed out. */
    memset (child_thread, 0, sizeof (*child_thread));
    
    /* Take the register snapshot from the current syscall, not a stale timer tick. */
    end_context (frame, parent_thread);
    
    /* Copy the parent thread registers to the child thread. */
    memcpy (&child_thread->regs, &parent_thread->regs, sizeof (child_thread->regs));
    
#if     defined (__AMD64__)
    child_thread->regs.rax = 0;
    child_thread->nt_teb_base = parent_thread->nt_teb_base;
    child_thread->kstack.rsp0 = (uintptr_t) (stack + 0x8000);
#else
    child_thread->regs.eax = 0;
    
    child_thread->kstack.esp0 = (uintptr_t) (stack + 0x8000);
    child_thread->kstack.ss0 = parent_thread->kstack.ss0;
    
    child_thread->tls_limit = parent_thread->tls_limit;
    child_thread->tls_access = parent_thread->tls_access;
    child_thread->tls_flags = parent_thread->tls_flags;
#endif
    
    child_thread->regs.cr3 = cloned_pd;
    
    child_thread->kstack.stack_start = (uintptr_t) stack;
    child_thread->tls_base = parent_thread->tls_base;
    
    child_thread->owner = process;
    child_thread->user_mode = 1;
    
    child_thread->threadId = generate_thread_id ();
    child_thread->birth_time = get_uptime_milliseconds ();
    
    thread_resume (child_thread);
    
    /* File descriptors are inherited; they must not be reopened. */
    for (i = 0; i < 256; i++) {
    
        if (process->fd[i]) {
            vfs_retain (process->fd[i]);
        }
    
    }
    
    p = g_current_thread;
    
    while (p->next) {
        p = (struct thread *) p->next;
    }
    
    p->next = child_thread;
    return (int) process->pid;

}

static void process_reap_zombies (struct thread *current) {

    struct thread *previous = g_first_thread, *thread;
    struct process *process;
    
    uint64_t total, free_before, free_after;
    uint32_t pid;
    
    while (previous && previous->next) {
    
        thread = (struct thread *) previous->next;
        process = thread->owner;
        
        if (thread == current || thread->state != TS_DEAD || !process || !process->exiting || (!process->reaped && process->parent != kernel_process)) {
        
            previous = thread;
            continue;
        
        }
        
        previous->next = thread->next;
        pid = process->pid;
        
        mmu_get_memory_info (&total, &free_before);
        dprintf ("[reap] pid=%u free=%llu kB\n", (unsigned) pid, free_before);
        
        if (process->pd && process->pd != kernel_page_directory_physical) {
            mmu_destroy_address_space (process->pd);
        }
        
        mmu_get_memory_info (&total, &free_after);
        dprintf ("[reap] pid=%u free=%llu kB\n", (unsigned) pid, free_after);
        
        if (thread->kstack.stack_start) {
            kfree ((void *) thread->kstack.stack_start);
        }
        
        kfree (process);
        kfree (thread);
        
        mmu_get_memory_info (&total, &free_after);
        dprintf ("[reap] pid=%u reclaimed=%llu kB\n", (unsigned) pid, free_after - free_before);
    
    }

}

int process_waitpid (int pid, uintptr_t status, int options, struct regs *regs) {

    struct thread *current = thread_get_current (), *thread;
    
    struct process *process;
    int children = 0, child_pid, result;
    
    if (!current || !current->user_mode || !current->owner || !regs) {
        return -EINVAL;
    }
    
    if ((pid < -1) || (options & ~1)) {
        return -EINVAL;
    }
    
    if (status && !mmu_user_buffer_valid (current->owner->pd, status, sizeof (int), 1)) {
        return -EFAULT;
    }
    
    for (thread = g_first_thread; thread; thread = (struct thread *) thread->next) {
    
        process = thread->owner;
        
        if (!process || process->parent != current->owner || process->reaped || (pid > 0 && process->pid != (uint32_t) pid)) {
            continue;
        }
        
        children = 1;
        
        if (thread->state != TS_DEAD || !process->exiting || process->reaped) {
            continue;
        }
        
        result = process->exit_status;
        child_pid = (int) process->pid;
        
        if (status && !mmu_copy_user (current->owner->pd, status, &result, sizeof (result), 1)) {
            return -EFAULT;
        }
        
        process->reaped = 1;
        
        process_reap_zombies (current);
        return child_pid;
    
    }
    
    if (!children) {
        return -ECHILD;
    }
    
    if (options & 1) {                 /* WNOHANG */
        return 0;
    }
    
    current->wait_pid = pid;
    current->wait_status = status;
    current->state = TS_WAITCHILD;
    
#if     defined (__AMD64__)
    regs->rax = 0;
#else
    regs->eax = 0;
#endif
    
    schedule (regs);
    return 0;

}

/* The shell currently has one foreground child while waiting for it. */
void process_interrupt_console (console_t *console) {

    struct thread *parent, *child;
    
    for (parent = g_first_thread; parent; parent = (struct thread *) parent->next) {
    
        if (!parent->owner || parent->owner->console != console || parent->state != TS_WAITCHILD) {
            continue;
        }
        
        for (child = g_first_thread; child; child = (struct thread *) child->next) {
        
            if (!child->user_mode || !child->owner || child->owner->exiting || child->owner->parent != parent->owner ||
                (parent->wait_pid > 0 && child->owner->pid != (uint32_t) parent->wait_pid)) {
                continue;
            }
            
            if (child->state == TS_WAITCHILD) {
                continue;
            }
            
            child->pending_signal = SIGINT;
            return;
        
        }
    
    }

}

/* Deliver the default signal action once a thread has a saved user context. */
static void process_dispatch_signals (void) {

    struct thread *thread, *parent, *child;
    
    struct ipc_request *request;
    struct process *process;
    
    int i;
    
    for (thread = g_first_thread; thread; thread = (struct thread *) thread->next) {
    
        if ((thread->pending_signal != SIGINT && thread->pending_signal != SIGPIPE) || !thread->user_mode || (thread->regs.cs & 3) != 3 ||
            thread->state == TS_DEAD || !thread->owner || thread->owner->exiting) {
            continue;
        }
        
        /* Do not discard a live request while a loader may still be using it. */
        for (request = ipc_requests; request; request = request->next) {
        
            if (request->sender == thread) {
                break;
            }
        
        }
        
        if (request) {
            continue;
        }
        
        process = thread->owner;
        process->exiting = 1;
        
        process->exit_status = (int) thread->pending_signal;
        thread->pending_signal = 0;
        
        if (process->console && process->console->input_reader == thread) {
            process->console->input_reader = 0;
        }
        
        for (i = 0; i < 256; i++) {
        
            if (process->fd[i]) {
                process_close_file (process, i);
            }
        
        }
        
        for (child = g_first_thread; child; child = (struct thread *) child->next) {
        
            if (child->owner && child->owner->parent == process) {
                child->owner->parent = kernel_process;
            }
        
        }
        
        thread->state = TS_DEAD;
        
        for (parent = g_first_thread; parent; parent = (struct thread *) parent->next) {
        
            int result = (int) process->pid;
            
            if (!parent->owner || parent->owner != process->parent || parent->state != TS_WAITCHILD ||
                (parent->wait_pid > 0 && parent->wait_pid != result)) {
                continue;
            }
            
            if (parent->wait_status && !mmu_copy_user (parent->owner->pd, parent->wait_status, &process->exit_status, sizeof (process->exit_status), 1)) {
                result = -EFAULT;
            } else {
                process->reaped = 1;
            }
            
#if     defined (__AMD64__)
            parent->regs.rax = (uintptr_t) result;
#else
            parent->regs.eax = (uintptr_t) result;
#endif
            
            parent->wait_pid = 0;
            parent->wait_status = 0;
            
            thread_resume (parent);
            break;
        
        }
    
    }

}

void process_exit (int status, struct regs *regs) {

    struct thread *current = thread_get_current (), *thread;
    
    struct process *process;
    int i;
    
    if (!current || !current->user_mode || !current->owner || !regs) {
        return;
    }
    
    process = current->owner;
    
    process->exit_status = (status & 255) << 8;
    process->exiting = 1;
    
    dprintf ("[exit] pid=%u\n", (unsigned) process->pid);
    
    for (i = 0; i < 256; i++) {
    
        if (process->fd[i]) {
            process_close_file (process, i);
        }
    
    }
    
    for (thread = g_first_thread; thread; thread = (struct thread *) thread->next) {
    
        if (thread->owner && thread->owner->parent == process) {
            thread->owner->parent = kernel_process;
        }
    
    }
    
    current->state = TS_DEAD;
    
    for (thread = g_first_thread; thread; thread = (struct thread *) thread->next) {
    
        int child_pid = (int) process->pid;
        int result = child_pid;
        
        if (!thread->owner || thread->owner != process->parent || thread->state != TS_WAITCHILD || (thread->wait_pid > 0 && thread->wait_pid != child_pid)) {
            continue;
        }
        
        if (thread->wait_status && !mmu_copy_user (thread->owner->pd, thread->wait_status, &process->exit_status, sizeof (process->exit_status), 1)) {
            result = -EFAULT;
        } else {
            process->reaped = 1;
        }
        
#if     defined (__AMD64__)
        thread->regs.rax = (uintptr_t) result;
#else
        thread->regs.eax = (uintptr_t) result;
#endif
        
        thread->wait_pid = 0;
        thread->wait_status = 0;
        
        thread_resume (thread);
        break;
    
    }
    
    schedule (regs);

}

static void thread_mark_sleep (struct thread *thread, uint64_t delay) {

    thread->wake = get_uptime_milliseconds () + delay;
    thread->state = TS_SLEEP;
    
}

void thread_sleep_from_syscall (uint64_t delay, struct regs *regs) {

    thread_mark_sleep (g_current_thread, delay);
    schedule (regs);

}

void thread_sleep (uint64_t delay) {

    struct thread *thread = g_current_thread;
    thread_mark_sleep (thread, delay);
    
    while (thread->state == TS_SLEEP) {
    
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("hlt");
#endif
    
    }

}



/******************************************************************************
 * @file            process.h
 *****************************************************************************/
#ifndef     _PROCESS_H
#define     _PROCESS_H

#include    <corazon/tasking/ipc.h>
#include    <corazon/vfs/node.h>
#include    <corazon/console.h>
#include    <corazon/mmu.h>
#include    <corazon/regs.h>
#include    <corazon/spin.h>

#include    <stdint.h>

enum thread_state {

    TS_RUN,
    TS_WAITIO,
    TS_WAITCHILD,
    TS_SLEEP,
    TS_SELECT,
    TS_SUSPEND,
    TS_CRITICAL,
    TS_UNINTERRUPTIBLE,
    TS_DEAD,
    TS_PIPE

};

enum select_state {

    SS_NOTSTARTED,
    SS_STARTED,
    SS_FINISHED,
    SS_ERROR

};

enum syscall_state {

    SYSCALL_NOTSTARTED,
    SYSCALL_STARTED,
    SYSCALL_FINISHED,
    SYSCALL_ERROR

};

#define     PROCESS_CWD_MAX             4096
#define     PROCESS_NAME_MAX            32

struct process {

    char name[PROCESS_NAME_MAX];
    char cwd[PROCESS_CWD_MAX];

    uint32_t pid;
    uintptr_t pd;
    
    char *brk_begin;
    char *brk_end;
    char *brk_next_unallocated_page_begin;
    
    struct process *parent;
    void *tty;
    
    struct ipc_node *ipc_node;          /* IPC node of the current process */
    struct ipc_node *ipc_working;       /* Working IPC node of the current process */
    
    struct fs_node *fd[256];
    uint32_t dir_index[256];
    
    int exiting;
    int reaped;
    int exit_status;
    
    console_t *console;
    
    uintptr_t module_base;
    size_t module_size;

};

struct thread {

    uint32_t threadId;
    
    __attribute__ ((packed)) struct {
    
#if     defined (__AMD64__)
        uint64_t rax, rcx, rdx, rbx;
        uint64_t rsp, rbp, rsi, rdi;
        uint64_t r8, r9, r10, r11;
        uint64_t r12, r13, r14, r15;
        uint64_t rip, rflags;
        uint64_t cs, ss, ds, es, fs, gs;
        uint64_t cr3;
#else
        uint32_t eax, ecx, edx, ebx;
        uint32_t esp, ebp, esi, edi;
        uint32_t eip, eflags;
        uint32_t cs:16, ss:16, ds:16, es:16, fs:16, gs:16;
        uint32_t cr3;
#endif
    
    } regs;
    
    __attribute__ ((packed)) struct {
    
#if     defined (__AMD64__)
        uint64_t rsp0;
#else
        uint32_t esp0;
        uint16_t ss0;
#endif
        
        uintptr_t stack_start;
    
    } kstack;

    struct {
    
        enum select_state select_state;
        
        int result;
        int nfds;
        
        uintptr_t read_address;
        uintptr_t write_address;
        uintptr_t except_address;
        
        uint8_t readfds[128];
        uint8_t writefds[128];
        uint8_t exceptfds[128];
        
        uint64_t deadline;
        int has_timeout;
    
    } select;
    
    uint32_t user_mode;
    
    uint32_t pending_signal_count;
    uint32_t pending_signal;
    
    void *signals;
    
    enum thread_state state;
    uint64_t wake;
    
    struct process *owner;
    void *state_private_data;
    
    int wait_pid;
    uintptr_t wait_status;
    
#if     defined (__AMD64__)
    uintptr_t tls_base;
    uintptr_t nt_teb_base;
#else
    uintptr_t tls_base;
    uintptr_t tls_limit;
    
    uint8_t tls_access;
    uint8_t tls_flags;
#endif
    
    uint32_t birth_time;
    uint32_t context_switch_count;
    uint32_t context_start_time;
    uint32_t context_end_time;
    uint32_t consumed_cpu_time_ms;
    uint32_t consumed_cpu_time_ms_at_prev_mark;
    uint32_t usage_cpu; //FromPrevMark
    uint32_t called_syscall_count;
    
    struct {
    
        uint32_t number;
        
        uintptr_t user_ip;
        uintptr_t arguments[5];
        uintptr_t return_value;
        
        enum syscall_state state;
    
    } last_syscall;
    
    void *message_queue;
    spin_lock_t message_queue_lock;
    
    struct thread *ipc_sender_head;
    struct thread *ipc_sender_next;
    
    ipc_message_t ipc_outgoing_message;
    uintptr_t ipc_receive_address;
    
    uint32_t ipc_send_to;
    uint32_t ipc_receive_from;
    uint32_t ipc_receive_type;
    
    int ipc_sending;
    int ipc_receiving;
    int ipc_sendrec;
    
    struct {
    
        struct fs_node *node;
        int write;
        
        uintptr_t address;
        size_t length, written;
    
    } pipe;
    
    struct Thread *next;

};

struct process_info {

    char name[PROCESS_NAME_MAX];
    uint32_t pid, ppid, tid;
    
    enum thread_state state;

};

int process_get_info (uint32_t index, struct process_info *info);
int process_get_fd (uint32_t pid, uint32_t index, uint32_t *fd);

struct thread *thread_create_kthread (void (*func) (void));
struct thread *thread_get_current (void);

int thread_destroy_kthread (struct thread *thread);
void tasking_initialize (void);

int process_add_file (struct process *process, struct fs_node *node);
int process_close_file (struct process *process, int __fd);

struct fs_node *process_get_file (struct process *process, int __fd);
int process_execve (const char *path, char *const argv[], char *const envp[], struct regs *regs);

int process_create_user_task (const void *code, size_t size, uintptr_t virtual_base);
int process_create_user_task_at (const void *code, size_t size, uintptr_t virtual_base, uintptr_t entry_point);
int process_create_user_process_from_file (const char *path);

int process_waitpid (int pid, uintptr_t status, int options, struct regs *regs);
void process_exit (int status, struct regs *regs);

void process_interrupt_console (console_t *console);

int process_exec_loader_start (const void *code, size_t size, uintptr_t virtual_base, uintptr_t entry_point);
int process_fork_from_clone (struct regs *frame, uintptr_t cloned_pd);

void thread_sleep_from_syscall (uint64_t delay, struct regs *regs);
void thread_sleep (uint64_t delay);

int ipc_send (uint32_t destination, const ipc_message_t *message, struct regs *regs);
int ipc_receive (uint32_t source, ipc_message_t *message, struct regs *regs);
int ipc_sendrec (uint32_t destination, ipc_message_t *message, struct regs *regs);

#endif      /* _PROCESS_H */

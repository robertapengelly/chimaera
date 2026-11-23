/******************************************************************************
 * @file            idt.c
 *****************************************************************************/
#include    <corazon/tasking/irq.h>
#include    <corazon/tasking/process.h>
#include    <corazon/utils/misc.h>
#include    <corazon/console.h>

#include    <sys/signal_defs.h>
#include    <stdint.h>

static struct idt_pointer idtp;
static idt_entry_t idt[256];

static void idt_set_gate (uint8_t num, interrupt_handler_t handler, uint16_t selector, uint8_t flags, int userspace) {

    uintptr_t base = (uintptr_t) handler;
    
    idt[num].base_low  = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    
    idt[num].selector = selector;
    idt[num].zero = 0;
    
    idt[num].flags = flags | (userspace ? 0x60 : 0);

}

void idt_install (void) {

    idtp.limit = sizeof (idt) - 1;
    idtp.base  = (uintptr_t) &idt;
    
    /** ISRs */
    idt_set_gate (0,   isr0,   0x08, 0x8E, 0);
    idt_set_gate (1,   isr1,   0x08, 0x8E, 0);
    idt_set_gate (2,   isr2,   0x08, 0x8E, 0);
    idt_set_gate (3,   isr3,   0x08, 0x8E, 0);
    idt_set_gate (4,   isr4,   0x08, 0x8E, 0);
    idt_set_gate (5,   isr5,   0x08, 0x8E, 0);
    idt_set_gate (6,   isr6,   0x08, 0x8E, 0);
    idt_set_gate (7,   isr7,   0x08, 0x8E, 0);
    idt_set_gate (8,   isr8,   0x08, 0x8E, 0);
    idt_set_gate (9,   isr9,   0x08, 0x8E, 0);
    idt_set_gate (10,  isr10,  0x08, 0x8E, 0);
    idt_set_gate (11,  isr11,  0x08, 0x8E, 0);
    idt_set_gate (12,  isr12,  0x08, 0x8E, 0);
    idt_set_gate (13,  isr13,  0x08, 0x8E, 0);
    idt_set_gate (14,  isr14,  0x08, 0x8E, 0);
    idt_set_gate (15,  isr15,  0x08, 0x8E, 0);
    idt_set_gate (16,  isr16,  0x08, 0x8E, 0);
    idt_set_gate (17,  isr17,  0x08, 0x8E, 0);
    idt_set_gate (18,  isr18,  0x08, 0x8E, 0);
    idt_set_gate (19,  isr19,  0x08, 0x8E, 0);
    idt_set_gate (20,  isr20,  0x08, 0x8E, 0);
    idt_set_gate (21,  isr21,  0x08, 0x8E, 0);
    idt_set_gate (22,  isr22,  0x08, 0x8E, 0);
    idt_set_gate (23,  isr23,  0x08, 0x8E, 0);
    idt_set_gate (24,  isr24,  0x08, 0x8E, 0);
    idt_set_gate (25,  isr25,  0x08, 0x8E, 0);
    idt_set_gate (26,  isr26,  0x08, 0x8E, 0);
    idt_set_gate (27,  isr27,  0x08, 0x8E, 0);
    idt_set_gate (28,  isr28,  0x08, 0x8E, 0);
    idt_set_gate (29,  isr29,  0x08, 0x8E, 0);
    idt_set_gate (30,  isr30,  0x08, 0x8E, 0);
    idt_set_gate (31,  isr31,  0x08, 0x8E, 0);
    
    /* IRQs */
    idt_set_gate (32,  irq0,   0x08, 0x8E, 0);
    idt_set_gate (33,  irq1,   0x08, 0x8E, 0);
    idt_set_gate (34,  irq2,   0x08, 0x8E, 0);
    idt_set_gate (35,  irq3,   0x08, 0x8E, 0);
    idt_set_gate (36,  irq4,   0x08, 0x8E, 0);
    idt_set_gate (37,  irq5,   0x08, 0x8E, 0);
    idt_set_gate (38,  irq6,   0x08, 0x8E, 0);
    idt_set_gate (39,  irq7,   0x08, 0x8E, 0);
    idt_set_gate (40,  irq8,   0x08, 0x8E, 0);
    idt_set_gate (41,  irq9,   0x08, 0x8E, 0);
    idt_set_gate (42,  irq10,  0x08, 0x8E, 0);
    idt_set_gate (43,  irq11,  0x08, 0x8E, 0);
    idt_set_gate (44,  irq12,  0x08, 0x8E, 0);
    idt_set_gate (45,  irq13,  0x08, 0x8E, 0);
    idt_set_gate (46,  irq14,  0x08, 0x8E, 0);
    idt_set_gate (47,  irq15,  0x08, 0x8E, 0);
    
    idt_set_gate (128, isr128, 0x08, 0x8E, 1);
    
    /* Initialize the IDT. */
    __asm__ __volatile__ ("lidt fword ptr %0" : : "m" (idtp));

}

static void panic (const char *desc, struct regs *r, uintptr_t faulting_address) {

    console_setbackgound (0, 0, 0);
    console_setforegound (170, 170, 170);
    
    console_clear ();
    
    /* Print the description, current process, cause */
    kprintf ("%s pid=%d (%s) at %#zx\n\n", desc, 0, "kernel", faulting_address);
    dprintf ("%s pid=%d (%s) at %#zx\n\n", desc, 0, "kernel", faulting_address);
    
    /* Dump register state */
    kprintf (
    
        "Registers at interrupt:\n\n"
        "  eip=0x%08lx\n\n"
        "  esi=0x%08lx, edi=0x%08lx\n"
        "  ebp=0x%08lx, esp=0x%08lx\n"
        "  eax=0x%08lx, ebx=0x%08lx\n"
        "  ecx=0x%08lx, edx=0x%08lx\n\n"
        "  cs=0x%08lx  ss=0x%08lx  eflags=0x%08lx\n"
        "  int=0x%02lx  err=0x%02lx\n",
        r->eip,
        r->esi, r->edi, r->ebp, r->esp,
        r->eax, r->ebx, r->ecx, r->edx,
        r->cs, r->ss, r->eflags, r->int_no, r->err_code
    
    );
    
    dprintf (
    
        "Registers at interrupt:\n\n"
        "  eip=0x%08lx\n\n"
        "  esi=0x%08lx, edi=0x%08lx\n"
        "  ebp=0x%08lx, esp=0x%08lx\n"
        "  eax=0x%08lx, ebx=0x%08lx\n"
        "  ecx=0x%08lx, edx=0x%08lx\n\n"
        "  cs=0x%08lx  ss=0x%08lx  eflags=0x%08lx\n"
        "  int=0x%02lx  err=0x%02lx\n",
        r->eip,
        r->esi, r->edi, r->ebp, r->esp,
        r->eax, r->ebx, r->ecx, r->edx,
        r->cs, r->ss, r->eflags, r->int_no, r->err_code
    
    );
    
    for (;;) {
    
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("hlt");
#endif
    
    }

}

/**
 * @brief Debug interrupt
 *
 * Called when a CPU is single-stepping. We need to reset
 * the single-step flag in RFLAGS and if we were actually
 * debugging the current process we need to trigger a ptrace
 * SINGLESTEP event. This should also return immediately
 * from the syscall handler.
 *
 * @param r Interrupt register context
 * @return Register context, which should be unmodified.
 */
static void debug_int(struct regs *r) {
    (void) r;
}

/** @brief Double fault should always panic. */
static void double_fault (struct regs *r) {
    panic ("Double fault", r, 0);
}

/**
 * @brief GPF handler.
 *
 * Mostly this is separated from other exceptions because
 * GPF should cause SIGSEGV rather than SIGILL? I think?
 *
 * @param r Interrupt register context
 */
static void general_protection_fault (struct regs *r) {

    /* Were we in the kernel? */
    if (r->cs == 0x08) {
    
        /* Then that's a panic. */
        panic ("GPF in kernel", r, 0);
    
    }
    
    panic ("GPF in user mode", r, 0);
    
    for (;;) {
    
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("hlt");
#endif
    
    }

}

enum fault_code {

    FAULT_CODE_FROM_KERNEL = 0x00000001,
    FAULT_CODE_READ        = 0x00000002,
    FAULT_CODE_WRITE       = 0x00000004,
    FAULT_CODE_INSTR       = 0x00000008,

};

enum fault_response {

    FAULT_RESPONSE_RESUME = 0,
    FAULT_RESPONSE_BAD_WRITE,
    FAULT_RESPONSE_BAD_READ,
    FAULT_RESPONSE_BAD_INSTR,
    FAULT_RESPONSE_NO_MAPPING,

};

/**
 * @brief Page fault handler.
 *
 * Handles magic return addresses, stack expansions, maybe
 * later will handle COW or mmap'd filed... otherwise,
 * mostly segfaults.
 *
 * @param r Interrupt register context
 */
static void page_fault (struct regs *r) {

    uintptr_t faulting_address;
    
    enum fault_code fault_flags = 0;
    /*enum fault_response fault_resp;*/
    
    __asm__ __volatile__ ("mov %0, cr2" : "=r" (faulting_address));
    
    if ((r->cs & 3) == 3) {
        panic ("Page fault in user mode", r, faulting_address);
    }
    
    /* Magic ret-from_sig_address. */
    if (faulting_address == 0x516) {
    
        for (;;) {
            __asm__ __volatile__ ("hlt");
        }
    
    }
    
    if ((r->err_code & 3) == 3) {
    
        for (;;) {
            __asm__ __volatile__ ("hlt");
        }
    
    }
    
    if (!(r->err_code & (1 << 2))) {
        fault_flags |= FAULT_CODE_FROM_KERNEL;
    }
    
    if (r->err_code & (1 << 4)) {
        fault_flags |= FAULT_CODE_INSTR;
    } else {
        fault_flags |= FAULT_CODE_READ;
    }
    
    if (r->err_code & (1 << 1)) {
        fault_flags |= FAULT_CODE_WRITE;
    }
    
    if (r->cs == 0x08) {
        panic ("Page fault in kernel", r, faulting_address);
    }
    
    /*fault_resp = generic_page_fault (faulting_address, fault_flags, r);
    
    if (fault_resp == FAULT_RESPONSE_RESUME) {
        return;
    }*/
    
    for (;;) {
    
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("hlt");
#endif
    
    }

}

/**
 * @brief Handle an exception interrupt.
 *
 * @param r           Interrupt register context
 * @param description Textual description of the exception, for panic messages.
 */
static void exception (struct regs *r, const char *description, int signum) {

    /* If we were in kernel space, this is a panic */
    if (r->cs == 0x08) {
        panic (description, r, r->int_no);
    }
    
    panic (description, r, r->int_no);
    
    (void) signum;
    
    for (;;) {
    
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("hlt");
#endif
    
    }

}

/** External IRQ management */
#define     IRQ_CHAIN_SIZE              16
#define     IRQ_CHAIN_DEPTH             4

static irq_handler_chain_t irq_routines[IRQ_CHAIN_SIZE * IRQ_CHAIN_DEPTH] = { 0 };
static const char *irq_handler_descriptions[IRQ_CHAIN_SIZE * IRQ_CHAIN_DEPTH] = { 0 };

void irq_install_handler (size_t irq, irq_handler_chain_t handler, const char *desc) {

    size_t i;
    
    for (i = 0; i < IRQ_CHAIN_DEPTH; i++) {
    
        if (irq_routines[i * IRQ_CHAIN_SIZE + irq]) {
            continue;
        }
        
        irq_routines[i * IRQ_CHAIN_SIZE + irq] = handler;
        irq_handler_descriptions[i * IRQ_CHAIN_SIZE + irq] = desc;
        
        break;
    
    }

}

static void handle_irq (struct regs *r, int irq) {

    size_t i;
    
    for (i = 0; i < IRQ_CHAIN_DEPTH; i++) {
    
        irq_handler_chain_t handler = irq_routines[i * IRQ_CHAIN_SIZE + irq];
        
        if (!handler) {
            break;
        }
        
        if (handler (r)) {
            return;
        }
    
    }
    
    /* Unhandled */
    irq_ack ();

}

#define     ISR_CHAIN_SIZE              256
#define     ISR_CHAIN_DEPTH             4

static isr_handler_chain_t isr_routines[ISR_CHAIN_SIZE * ISR_CHAIN_DEPTH] = { 0 };
static const char *isr_handler_descriptions[ISR_CHAIN_SIZE * ISR_CHAIN_DEPTH] = { 0 };

void isr_install_handler (size_t isr, isr_handler_chain_t handler, const char *desc) {

    size_t i;
    
    for (i = 0; i < ISR_CHAIN_DEPTH; i++) {
    
        if (isr_routines[i * ISR_CHAIN_SIZE + isr]) {
            continue;
        }
        
        isr_routines[i * ISR_CHAIN_SIZE + isr] = handler;
        isr_handler_descriptions[i * ISR_CHAIN_SIZE + isr] = desc;
        
        break;
    
    }

}

static void handle_isr (struct regs *r, int isr) {

    size_t i;
    
    for (i = 0; i < ISR_CHAIN_DEPTH; i++) {
    
        irq_handler_chain_t handler = isr_routines[i * ISR_CHAIN_SIZE + isr];
        
        if (!handler) {
            break;
        }
        
        if (handler (r)) {
            return;
        }
    
    }
    
    /* Unhandled */
    irq_ack ();

}

#define     EXC(i,n,s)                  case i: exception (r, n, s); break;

#define     IRQ(i)                      case i: handle_irq (r, i - 32); break;
#define     ISR(i)                      case i: handle_isr (r, i); break;

void isr_handler (struct regs *r) {

    switch (r->int_no) {
    
        EXC (0, "divide-by-zero", SIGFPE);
        
        case 1:
        
            debug_int (r);
            return;
        
        /* NMI doesn't reach here, we use it as a panic signal. */
        EXC (3, "breakpoint", SIGTRAP)      /* TODO: This should map to a ptrace event */
        EXC (4, "overflow", SIGFPE)
        EXC (5, "bound range exceeded", SIGBUS)
        EXC (6, "invalid opcode", SIGILL)
        EXC (7, "device not available", SIGBUS)
        
        case 8:
        
            double_fault (r);
            break;
        
        /* 9 is a legacy exception that shouldn't happen */
        EXC (10, "invalid TSS", SIGBUS)
        EXC (11, "segment not present", SIGBUS)
        EXC (12, "stack-segment fault", SIGBUS)
        
        case 13:
        
            general_protection_fault (r);
            break;
        
        case 14:
        
            page_fault (r);
            break;
        
        /* 15 is reserved */
        EXC (16, "floating point exception", SIGFPE)
        EXC (17, "alignment check", SIGBUS)
        EXC (18, "machine check", SIGBUS)
        EXC (19, "SIMD floating-point exception", SIGFPE)
        EXC (20, "virtualization exception", SIGBUS)
        EXC (21, "control protection exception", SIGBUS)
        
        /* 22 through 27 are reserved */
        EXC (28, "hypervisor injection exception", SIGBUS)
        EXC (29, "VMM communication exception", SIGBUS)
        EXC (30, "security exception", SIGBUS)
        
        /* 31 is reserved */
        
        /* 16 IRQs that go to the general IRQ chain */
        IRQ (32)
        IRQ (33)
        IRQ (34)
        IRQ (35)
        IRQ (36)
        IRQ (37)
        IRQ (38)
        
        case 39:
        
            break;  /* Except the spurious IRQ, just ignore that */
        
        IRQ (40)
        IRQ (41)
        IRQ (42)
        IRQ (43)
        IRQ (44)
        IRQ (45)
        IRQ (46)
        IRQ (47)
        
        ISR (128)
        
        default:
        
            panic ("Unexpected interrupt", r, 0);
            break;
    
    }

}

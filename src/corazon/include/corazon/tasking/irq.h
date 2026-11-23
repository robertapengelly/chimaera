/******************************************************************************
 * @file            irq.h
 *****************************************************************************/
#ifndef     _IRQ_H
#define     _IRQ_H

#include    <corazon/regs.h>

#include    <stddef.h>
#include    <stdint.h>

extern struct regs *isr0 (struct regs *);
extern struct regs *isr1 (struct regs *);
extern struct regs *isr2 (struct regs *);
extern struct regs *isr3 (struct regs *);
extern struct regs *isr4 (struct regs *);
extern struct regs *isr5 (struct regs *);
extern struct regs *isr6 (struct regs *);
extern struct regs *isr7 (struct regs *);
extern struct regs *isr8 (struct regs *);
extern struct regs *isr9 (struct regs *);
extern struct regs *isr10 (struct regs *);
extern struct regs *isr11 (struct regs *);
extern struct regs *isr12 (struct regs *);
extern struct regs *isr13 (struct regs *);
extern struct regs *isr14 (struct regs *);
extern struct regs *isr15 (struct regs *);
extern struct regs *isr16 (struct regs *);
extern struct regs *isr17 (struct regs *);
extern struct regs *isr18 (struct regs *);
extern struct regs *isr19 (struct regs *);
extern struct regs *isr20 (struct regs *);
extern struct regs *isr21 (struct regs *);
extern struct regs *isr22 (struct regs *);
extern struct regs *isr23 (struct regs *);
extern struct regs *isr24 (struct regs *);
extern struct regs *isr25 (struct regs *);
extern struct regs *isr26 (struct regs *);
extern struct regs *isr27 (struct regs *);
extern struct regs *isr28 (struct regs *);
extern struct regs *isr29 (struct regs *);
extern struct regs *isr30 (struct regs *);
extern struct regs *isr31 (struct regs *);

extern struct regs *irq0 (struct regs *);
extern struct regs *irq1 (struct regs *);
extern struct regs *irq2 (struct regs *);
extern struct regs *irq3 (struct regs *);
extern struct regs *irq4 (struct regs *);
extern struct regs *irq5 (struct regs *);
extern struct regs *irq6 (struct regs *);
extern struct regs *irq7 (struct regs *);
extern struct regs *irq8 (struct regs *);
extern struct regs *irq9 (struct regs *);
extern struct regs *irq10 (struct regs *);
extern struct regs *irq11 (struct regs *);
extern struct regs *irq12 (struct regs *);
extern struct regs *irq13 (struct regs *);
extern struct regs *irq14 (struct regs *);
extern struct regs *irq15 (struct regs *);

extern struct regs *isr123 (struct regs*);
extern struct regs *isr128 (struct regs *);

typedef struct regs *(*interrupt_handler_t) (struct regs *);

#if     defined (__AMD64__)
/** Interrupt descriptor table */
__attribute__((packed)) typedef struct {

    uint16_t base_low;
    uint16_t selector;
    
    uint8_t zero;
    uint8_t flags;
    
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t pad;

} idt_entry_t;
#else
/** Interrupt descriptor table */
__attribute__((packed)) typedef struct {

    uint16_t base_low;
    uint16_t selector;
    
    uint8_t zero;
    uint8_t flags;
    
    uint16_t base_high;

} idt_entry_t;
#endif

extern void irq_ack (void);

__attribute__((packed)) struct idt_pointer {

    uint16_t  limit;
    uintptr_t base;

};

typedef int (*irq_handler_chain_t) (struct regs *r);
typedef int (*isr_handler_chain_t) (struct regs *r);

void irq_install_handler (size_t irq, irq_handler_chain_t handler, const char *desc);
void isr_install_handler (size_t isr, isr_handler_chain_t handler, const char *desc);

int wait_for_irq (int no, uint32_t timeout);

#endif      /* _IRQ_H */

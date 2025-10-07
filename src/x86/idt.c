#include "x86/idt.h"

#include "memory.h"
#include "x86/gdt.h"

#include <stdint.h>

struct __attribute__((packed)) idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct __attribute__((packed)) idt_pointer {
    uint16_t limit;
    uint64_t base;
};

_Static_assert(sizeof(struct idt_gate) == 16, "x86-64 IDT gate size");

static struct idt_gate idt[256] __attribute__((aligned(16)));
extern const uintptr_t exception_stub_table[32];
extern void syscall_entry(void);
extern void timer_interrupt_entry(void);
extern void serial_interrupt_entry(void);

static void set_gate(unsigned int vector, uintptr_t handler, uint8_t ist,
                     uint8_t attributes)
{
    struct idt_gate *gate = &idt[vector];
    gate->offset_low = (uint16_t)handler;
    gate->selector = GDT_KERNEL_CODE;
    gate->ist = ist & 0x07;
    gate->attributes = attributes;
    gate->offset_middle = (uint16_t)(handler >> 16);
    gate->offset_high = (uint32_t)(handler >> 32);
    gate->reserved = 0;
}

void idt_init(void)
{
    const struct idt_pointer pointer = {
        .limit = sizeof(idt) - 1,
        .base = (uintptr_t)&idt,
    };

    memset(idt, 0, sizeof(idt));
    for (unsigned int vector = 0; vector < 32; ++vector) {
        set_gate(vector, exception_stub_table[vector], vector == 8 ? 1 : 0,
                 0x8e); /* Present, ring-0, 64-bit interrupt gate. */
    }
    set_gate(0x80, (uintptr_t)syscall_entry, 0, 0xee);
    set_gate(32, (uintptr_t)timer_interrupt_entry, 0, 0x8e);
    set_gate(33, (uintptr_t)serial_interrupt_entry, 0, 0x8e);
    __asm__ volatile ("lidt %0" : : "m"(pointer));
}

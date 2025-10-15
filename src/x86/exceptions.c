#include "x86/exceptions.h"

#include "kernel/process.h"
#include "panic.h"
#include "print.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(offsetof(struct exception_frame, vector) == 120,
               "assembly exception frame layout");

static const char *const exception_names[32] = {
    "divide error (#DE)", "debug (#DB)", "non-maskable interrupt",
    "breakpoint (#BP)", "overflow (#OF)", "bound range exceeded (#BR)",
    "invalid opcode (#UD)", "device not available (#NM)",
    "double fault (#DF)", "coprocessor segment overrun", "invalid TSS (#TS)",
    "segment not present (#NP)", "stack-segment fault (#SS)",
    "general protection fault (#GP)", "page fault (#PF)", "reserved",
    "x87 floating-point exception (#MF)", "alignment check (#AC)",
    "machine check (#MC)", "SIMD floating-point exception (#XM)",
    "virtualization exception (#VE)", "control protection exception (#CP)",
    "reserved", "reserved", "reserved", "reserved", "reserved", "reserved",
    "hypervisor injection exception (#HV)",
    "VMM communication exception (#VC)", "security exception (#SX)", "reserved",
};

static uint64_t read_cr2(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static uint64_t interrupted_rsp(const struct exception_frame *frame)
{
    if ((frame->cs & 3) != 0 || frame->vector == 8) {
        return frame->rsp;
    }
    return (uintptr_t)&frame->rsp;
}

static void print_page_fault_error(uint64_t error)
{
    kprintf("page fault: %s, %s, %s, %s\n",
            (error & 1) != 0 ? "protection violation" : "page not present",
            (error & 2) != 0 ? "write" : "read",
            (error & 4) != 0 ? "user" : "supervisor",
            (error & 16) != 0 ? "instruction fetch" : "data access");
}

struct exception_frame *exception_dispatch(struct exception_frame *frame)
{
    const uint64_t vector = frame->vector;
    const char *name = vector < 32 ? exception_names[vector] : "unknown";

    if ((frame->cs & 3) == 3) {
        log_warn("user exception: %s, rip=0x%lx, error=0x%lx", name,
                 (unsigned long)frame->rip,
                 (unsigned long)frame->error_code);
        if (vector == 14) {
            log_warn("user page fault address: 0x%lx",
                     (unsigned long)read_cr2());
        }
        return process_handle_fault(frame, vector == 14 ? read_cr2() : 0);
    }

    kprintf("\n[panic] exception: %s\n", name);
    kprintf("vector: %lu  error: 0x%lx\n",
            (unsigned long)vector, (unsigned long)frame->error_code);
    kprintf("rip: 0x%lx  rsp: 0x%lx\n",
            (unsigned long)frame->rip, (unsigned long)interrupted_rsp(frame));
    kprintf("cs: 0x%lx  rflags: 0x%lx\n",
            (unsigned long)frame->cs, (unsigned long)frame->rflags);
    kprintf("rax: 0x%lx  rbx: 0x%lx  rcx: 0x%lx\n",
            (unsigned long)frame->rax, (unsigned long)frame->rbx,
            (unsigned long)frame->rcx);
    kprintf("rdx: 0x%lx  rsi: 0x%lx  rdi: 0x%lx\n",
            (unsigned long)frame->rdx, (unsigned long)frame->rsi,
            (unsigned long)frame->rdi);
    kprintf("rbp: 0x%lx   r8: 0x%lx   r9: 0x%lx\n",
            (unsigned long)frame->rbp, (unsigned long)frame->r8,
            (unsigned long)frame->r9);
    kprintf("r10: 0x%lx  r11: 0x%lx  r12: 0x%lx\n",
            (unsigned long)frame->r10, (unsigned long)frame->r11,
            (unsigned long)frame->r12);
    kprintf("r13: 0x%lx  r14: 0x%lx  r15: 0x%lx\n",
            (unsigned long)frame->r13, (unsigned long)frame->r14,
            (unsigned long)frame->r15);

    if (vector == 14) {
        kprintf("cr2: 0x%lx\n", (unsigned long)read_cr2());
        print_page_fault_error(frame->error_code);
    }
    halt_forever();
}

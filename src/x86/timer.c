#include "x86/timer.h"

#include "kernel/process.h"
#include "mm/address_space.h"
#include "panic.h"
#include "print.h"
#include "x86/io.h"

#include <stdint.h>

enum {
    PIC_MASTER_COMMAND = 0x20,
    PIC_MASTER_DATA = 0x21,
    PIC_SLAVE_COMMAND = 0xa0,
    PIC_SLAVE_DATA = 0xa1,
    IA32_APIC_BASE = 0x1b,
    APIC_TPR = 0x080,
    APIC_EOI = 0x0b0,
    APIC_SIVR = 0x0f0,
    APIC_LVT_TIMER = 0x320,
    APIC_INITIAL_COUNT = 0x380,
    APIC_DIVIDE_CONFIGURATION = 0x3e0,
};

static volatile uint64_t tick_count;
static volatile uint32_t *local_apic;
static const uint64_t LOCAL_APIC_VIRTUAL = 0xffffffff90000000ULL;

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(0));
}

static uint64_t read_msr(uint32_t index)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(index));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t index, uint64_t value)
{
    __asm__ volatile ("wrmsr" : : "c"(index), "a"((uint32_t)value),
                      "d"((uint32_t)(value >> 32)));
}

static void apic_write(uint32_t offset, uint32_t value)
{
    local_apic[offset / sizeof(uint32_t)] = value;
    (void)local_apic[APIC_TPR / sizeof(uint32_t)];
}

static void mask_legacy_pic(void)
{
    outb(PIC_MASTER_DATA, 0xff);
    outb(PIC_SLAVE_DATA, 0xff);
}

void timer_init(uint32_t initial_count)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    (void)eax;
    (void)ebx;
    (void)ecx;
    (void)edx;
    ASSERT((edx & (1u << 9)) != 0); /* Local APIC. */
    ASSERT(initial_count != 0);

    mask_legacy_pic();
    uint64_t apic_base = read_msr(IA32_APIC_BASE);
    const physical_address_t apic_physical = apic_base & 0xfffff000ULL;
    apic_base = (apic_base | (1ULL << 11)) & ~(1ULL << 10);
    write_msr(IA32_APIC_BASE, apic_base);
    struct address_space kernel_space = address_space_current();
    ASSERT(address_space_map_kernel(&kernel_space, LOCAL_APIC_VIRTUAL,
                                    apic_physical, PAGE_WRITABLE));
    local_apic = (volatile uint32_t *)(uintptr_t)LOCAL_APIC_VIRTUAL;
    apic_write(APIC_TPR, 0);
    apic_write(APIC_SIVR, (1u << 8) | 0xffu);
    apic_write(APIC_DIVIDE_CONFIGURATION, 0x0b); /* Divide by one. */
    apic_write(APIC_LVT_TIMER, (1u << 17) | 32u); /* Periodic, vector 32. */
    apic_write(APIC_INITIAL_COUNT, initial_count);
    log_info("xAPIC periodic timer armed: initial count %u", initial_count);
}

uint64_t timer_ticks(void)
{
    return tick_count;
}

void timer_end_interrupt(void)
{
    apic_write(APIC_EOI, 0);
}

struct exception_frame *timer_dispatch(struct exception_frame *frame)
{
    ++tick_count;
    timer_end_interrupt();
    if ((frame->cs & 3) == 0) return frame;
    return process_timer_tick(frame);
}

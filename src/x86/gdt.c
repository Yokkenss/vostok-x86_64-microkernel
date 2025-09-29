#include "x86/gdt.h"

#include "memory.h"

#include <stddef.h>
#include <stdint.h>

struct __attribute__((packed)) task_state_segment {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
};

struct __attribute__((packed)) tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t limit_high_flags;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
};

struct __attribute__((packed, aligned(16))) gdt_table {
    uint64_t null_descriptor;
    uint64_t kernel_code;
    uint64_t kernel_data;
    uint64_t user_data;
    uint64_t user_code;
    struct tss_descriptor tss;
};

struct __attribute__((packed)) descriptor_pointer {
    uint16_t limit;
    uint64_t base;
};

_Static_assert(sizeof(struct task_state_segment) == 104, "x86-64 TSS size");
_Static_assert(offsetof(struct gdt_table, tss) == GDT_TSS, "TSS selector");

static struct task_state_segment tss;
static struct gdt_table gdt;
static uint8_t privilege_stack[16384] __attribute__((aligned(16)));
static uint8_t double_fault_stack[16384] __attribute__((aligned(16)));

extern void gdt_load(const struct descriptor_pointer *pointer);

static void set_tss_descriptor(struct tss_descriptor *descriptor,
                               uintptr_t base, uint32_t limit)
{
    descriptor->limit_low = (uint16_t)limit;
    descriptor->base_low = (uint16_t)base;
    descriptor->base_middle = (uint8_t)(base >> 16);
    descriptor->access = 0x89; /* Present, available 64-bit TSS. */
    descriptor->limit_high_flags = (uint8_t)(limit >> 16) & 0x0f;
    descriptor->base_high = (uint8_t)(base >> 24);
    descriptor->base_upper = (uint32_t)(base >> 32);
    descriptor->reserved = 0;
}

void gdt_init(void)
{
    const struct descriptor_pointer pointer = {
        .limit = sizeof(gdt) - 1,
        .base = (uintptr_t)&gdt,
    };

    memset(&tss, 0, sizeof(tss));
    memset(&gdt, 0, sizeof(gdt));

    gdt.kernel_code = 0x00af9a000000ffffULL;
    gdt.kernel_data = 0x00cf92000000ffffULL;
    gdt.user_data = 0x00cff2000000ffffULL;
    gdt.user_code = 0x00affa000000ffffULL;

    tss.rsp[0] = (uintptr_t)(privilege_stack + sizeof(privilege_stack));
    tss.ist[0] = (uintptr_t)(double_fault_stack + sizeof(double_fault_stack));
    tss.io_map_base = sizeof(tss);
    set_tss_descriptor(&gdt.tss, (uintptr_t)&tss, sizeof(tss) - 1);

    gdt_load(&pointer);
}

void gdt_set_kernel_stack(uint64_t stack_top)
{
    tss.rsp[0] = stack_top;
}

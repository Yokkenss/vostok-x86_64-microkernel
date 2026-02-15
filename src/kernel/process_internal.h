#ifndef VOSTOK_KERNEL_PROCESS_INTERNAL_H
#define VOSTOK_KERNEL_PROCESS_INTERNAL_H

#include "mm/address_space.h"
#include "x86/exceptions.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    PROCESS_LIMIT = 8,
    CAPABILITY_LIMIT = 16,
    OWNED_PAGE_LIMIT = 128,
    KERNEL_STACK_SIZE = 16384,
    USER_STACK_PAGES = 4,
    CAPABILITY_TYPE_ENDPOINT = 1,
    CAPABILITY_TYPE_CONSOLE = 2,
};

enum process_state {
    PROCESS_UNUSED,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED_SEND,
    PROCESS_BLOCKED_RECEIVE,
    PROCESS_BLOCKED_CONSOLE,
    PROCESS_DEAD,
};

struct endpoint;

struct capability_slot {
    void *object;
    uint16_t generation;
    uint8_t type;
    uint8_t rights;
};

struct owned_page {
    uint64_t virtual_address;
    physical_address_t physical_address;
};

struct process {
    uint32_t pid;
    const char *name;
    enum process_state state;
    struct address_space address_space;
    struct exception_frame *frame;
    struct capability_slot capabilities[CAPABILITY_LIMIT];
    struct owned_page owned_pages[OWNED_PAGE_LIMIT];
    size_t owned_page_count;
    int exit_code;
    bool expected_fault;
    bool faulted;
    uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
};

struct endpoint {
    const char *name;
    bool trace;
    struct process *receiver;
    struct process *sender;
    uint64_t word;
    uint32_t transferred_capability;
    uint8_t transferred_rights;
    uint64_t send_started;
};

extern struct process processes[PROCESS_LIMIT];
extern struct process *current_process;

void process_cleanup(struct process *process);
struct exception_frame *scheduler_schedule(void);
void scheduler_initialize(struct address_space kernel_space);
void scheduler_dump_processes(void);
void scheduler_dump_metrics(void);
bool scheduler_demo_ready(void);

static inline uint64_t cpu_read_tsc(void)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

#endif

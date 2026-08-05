#include "kernel/process.h"

#include "kernel/ipc.h"
#include "kernel/process_internal.h"
#include "mm/physical.h"
#include "panic.h"
#include "print.h"
#include "serial.h"
#include "x86/gdt.h"
#include "x86/timer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct process processes[PROCESS_LIMIT];
struct process *current_process;

static struct address_space kernel_address_space;
static size_t initial_free_pages;
static uint64_t context_switch_count;
static uint64_t context_switch_cycles;
static uint64_t preemption_count;

static const char *state_name(enum process_state state)
{
    switch (state) {
    case PROCESS_UNUSED: return "unused";
    case PROCESS_READY: return "ready";
    case PROCESS_RUNNING: return "running";
    case PROCESS_BLOCKED_SEND: return "send-wait";
    case PROCESS_BLOCKED_RECEIVE: return "recv-wait";
    case PROCESS_BLOCKED_CONSOLE: return "console-wait";
    case PROCESS_BLOCKED_WAIT: return "process-wait";
    case PROCESS_DEAD: return "dead";
    }
    return "invalid";
}

void scheduler_dump_processes(void)
{
    kprintf("PID  NAME       STATE      EXIT  FAULT\n");
    for (size_t index = 0; index < PROCESS_LIMIT; ++index) {
        const struct process *process = &processes[index];
        if (process->state == PROCESS_UNUSED) continue;
        kprintf("%u    %s  %s  %d  %u\n", process->pid, process->name,
                state_name(process->state), process->exit_code,
                process->faulted ? 1u : 0u);
    }
}

void scheduler_dump_metrics(void)
{
    log_info("scheduler metrics: %lu switches, %lu preemptions, %lu ticks",
             (unsigned long)context_switch_count,
             (unsigned long)preemption_count,
             (unsigned long)timer_ticks());
    log_info("scheduler switch cost: %lu total cycles, %lu average cycles",
             (unsigned long)context_switch_cycles,
             (unsigned long)(context_switch_count == 0 ? 0
                 : context_switch_cycles / context_switch_count));
    ipc_dump_metrics();
    log_info("UART metrics: %lu receive interrupts",
             serial_interrupt_count());
}

bool scheduler_demo_ready(void)
{
    for (size_t index = 0; index < 6; ++index) {
        if (processes[index].state != PROCESS_DEAD) return false;
    }
    return true;
}

void scheduler_initialize(struct address_space kernel_space)
{
    kernel_address_space = kernel_space;
    initial_free_pages = physical_memory_stats().free_pages;
}

static size_t process_index(const struct process *process)
{
    return (size_t)(process - processes);
}

static bool all_processes_dead(void)
{
    for (size_t index = 0; index < PROCESS_LIMIT; ++index) {
        if (processes[index].state != PROCESS_DEAD
            && processes[index].state != PROCESS_UNUSED) return false;
    }
    return true;
}

static struct process *next_ready_process(void)
{
    const size_t start = current_process == NULL
                       ? 0 : process_index(current_process) + 1;
    for (size_t offset = 0; offset < PROCESS_LIMIT; ++offset) {
        struct process *candidate =
            &processes[(start + offset) % PROCESS_LIMIT];
        if (candidate->state == PROCESS_READY) return candidate;
    }
    return NULL;
}

static bool console_input_waiting(void)
{
    for (size_t index = 0; index < PROCESS_LIMIT; ++index) {
        if (processes[index].state == PROCESS_BLOCKED_CONSOLE) return true;
    }
    return false;
}

static void finish_demo(struct process *previous)
{
    address_space_activate(&kernel_address_space);
    if (previous != NULL && previous->state == PROCESS_DEAD) {
        process_cleanup(previous);
    }
    bool success = true;
    for (size_t index = 0; index < PROCESS_LIMIT; ++index) {
        struct process *process = &processes[index];
        if (process->state == PROCESS_UNUSED) continue;
        if (process != previous
            && process->address_space.root != INVALID_PHYSICAL_ADDRESS) {
            process_cleanup(process);
        }
        if ((process->expected_fault && !process->faulted)
            || (!process->expected_fault && process->exit_code != 0)) {
            success = false;
        }
        log_info("PID %u %s state=%s exit=%d faulted=%u", process->pid,
                 process->name, state_name(process->state),
                 process->exit_code, process->faulted ? 1u : 0u);
    }
    if (!success) panic("a userspace process failed its expected outcome");
    if (preemption_count < 2) {
        panic("preemption test did not observe two timer context switches");
    }
    const struct physical_memory_stats memory = physical_memory_stats();
    ASSERT(memory.free_pages == initial_free_pages);
    scheduler_dump_metrics();
    log_info("process memory reclaimed: %lu free pages",
             (unsigned long)memory.free_pages);
    log_info("all user processes completed successfully");
    halt_forever();
}

struct exception_frame *scheduler_schedule(void)
{
    uint64_t switch_started = cpu_read_tsc();
    bool idled = false;
    struct process *previous = current_process;
    struct process *next = next_ready_process();
    while (next == NULL) {
        if (all_processes_dead()) finish_demo(previous);
        if (!console_input_waiting())
            panic("IPC deadlock: no runnable process");
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
        idled = true;
        next = next_ready_process();
    }
    if (idled) switch_started = cpu_read_tsc();

    next->state = PROCESS_RUNNING;
    current_process = next;
    address_space_activate(&next->address_space);
    gdt_set_kernel_stack((uintptr_t)next->kernel_stack
                         + sizeof(next->kernel_stack));
    if (previous != NULL && previous->state == PROCESS_DEAD) {
        process_cleanup(previous);
    }
    if (previous != next) {
        ++context_switch_count;
        context_switch_cycles += cpu_read_tsc() - switch_started;
    }
    return next->frame;
}

void process_serial_wake(void)
{
    for (size_t index = 0; index < PROCESS_LIMIT; ++index) {
        struct process *process = &processes[index];
        if (process->state != PROCESS_BLOCKED_CONSOLE) continue;
        const int character = serial_try_getc();
        if (character < 0) return;
        process->frame->rax = (uint64_t)character;
        process->state = PROCESS_READY;
        return;
    }
}

struct exception_frame *process_timer_tick(struct exception_frame *frame)
{
    if (current_process == NULL) return frame;
    ASSERT(current_process->state == PROCESS_RUNNING);
    current_process->frame = frame;
    current_process->state = PROCESS_READY;
    ++preemption_count;
    return scheduler_schedule();
}

struct exception_frame *process_handle_fault(struct exception_frame *frame,
                                             uint64_t fault_address)
{
    ASSERT(current_process != NULL);
    current_process->frame = frame;
    current_process->exit_code = 128 + (int)frame->vector;
    current_process->faulted = true;
    current_process->state = PROCESS_DEAD;
    process_wake_waiters(current_process->pid, current_process->exit_code);
    log_warn("process %s pid=%u terminated by exception %lu at 0x%lx",
             current_process->name, current_process->pid,
             (unsigned long)frame->vector, (unsigned long)fault_address);
    return scheduler_schedule();
}

#include "kernel/process.h"

#include "abi/syscall.h"
#include "boot/limine_requests.h"
#include "kernel/capability.h"
#include "kernel/elf.h"
#include "kernel/ipc.h"
#include "kernel/process_internal.h"
#include "memory.h"
#include "mm/address_space.h"
#include "panic.h"
#include "print.h"
#include "serial.h"
#include "x86/gdt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const uint64_t USER_STACK_TOP = 0x0000700000000000ULL;
static struct endpoint main_endpoint = { .name = "main", .trace = true };
static struct endpoint audit_endpoint = { .name = "audit", .trace = true };
static struct endpoint console_endpoint = { .name = "console" };
static uint8_t console_object;

static bool console_authorized(uint32_t handle, uint8_t rights,
                               int64_t *result)
{
    return capability_lookup_object(current_process, handle,
                                    CAPABILITY_TYPE_CONSOLE, rights,
                                    result) != NULL;
}

extern __attribute__((noreturn))
void process_enter(struct exception_frame *frame);

static const struct limine_file *find_module(const char *name)
{
    const struct limine_module_response *modules = boot_modules();
    if (modules == NULL) return NULL;
    for (uint64_t index = 0; index < modules->module_count; ++index) {
        const struct limine_file *module = modules->modules[index];
        const char *left = module->string;
        const char *right = name;
        if (left == NULL) continue;
        while (*left != '\0' && *left == *right) {
            ++left;
            ++right;
        }
        if (*left == '\0' && *right == '\0') return module;
    }
    return NULL;
}

void process_cleanup(struct process *process)
{
    capability_cleanup(process);
    user_image_destroy(process);
}

static bool process_create(size_t index, const char *name)
{
    struct process *process = &processes[index];
    memset(process, 0, sizeof(*process));
    process->pid = (uint32_t)index + 1;
    process->name = name;
    if (!address_space_create(&process->address_space)) return false;

    uint64_t entry;
    if (!user_image_load_elf(process, find_module(name), &entry)) {
        process_cleanup(process);
        return false;
    }
    for (size_t page = 0; page < USER_STACK_PAGES; ++page) {
        const uint64_t address = USER_STACK_TOP - (page + 1) * PAGE_SIZE;
        if (!user_image_map_page(process, address,
                                 PAGE_USER | PAGE_WRITABLE)) {
            process_cleanup(process);
            return false;
        }
    }

    uintptr_t stack_top = (uintptr_t)process->kernel_stack
                        + sizeof(process->kernel_stack);
    stack_top &= ~(uintptr_t)0xf;
    process->frame = (void *)(stack_top - sizeof(*process->frame));
    memset(process->frame, 0, sizeof(*process->frame));
    process->frame->rip = entry;
    process->frame->cs = GDT_USER_CODE | 3;
    process->frame->rflags = 0x202;
    process->frame->rsp = USER_STACK_TOP;
    process->frame->ss = GDT_USER_DATA | 3;
    process->state = PROCESS_READY;
    log_info("process %s pid=%u loaded: entry=0x%lx, isolated CR3=0x%lx",
             name, process->pid, (unsigned long)entry,
             (unsigned long)process->address_space.root);
    return true;
}

struct exception_frame *syscall_dispatch(struct exception_frame *frame)
{
    static bool logged_first_syscall;
    ASSERT(current_process != NULL);
    if (!logged_first_syscall) {
        log_info("first ring-3 syscall rflags=0x%lx",
                 (unsigned long)frame->rflags);
        logged_first_syscall = true;
    }
    current_process->frame = frame;
    switch (frame->rax) {
    case VOSTOK_SYS_EXIT:
        current_process->exit_code = (int)frame->rdi;
        log_info("process %s pid=%u exited with code %d",
                 current_process->name, current_process->pid,
                 current_process->exit_code);
        current_process->state = PROCESS_DEAD;
        return scheduler_schedule();
    case VOSTOK_SYS_IPC_SEND:
        return ipc_send(frame);
    case VOSTOK_SYS_IPC_RECEIVE:
        return ipc_receive(frame);
    case VOSTOK_SYS_CAP_CLOSE:
        frame->rax = (uint64_t)capability_close(current_process,
                                                (uint32_t)frame->rdi);
        return frame;
    case VOSTOK_SYS_CONSOLE_PUTC:
    {
        int64_t result;
        if (!console_authorized((uint32_t)frame->rdi,
                                VOSTOK_RIGHT_CONSOLE_WRITE, &result)) {
            log_info("capability check rejected console write by %s: error=%ld",
                     current_process->name, (long)result);
            frame->rax = (uint64_t)result;
        } else if (frame->rsi > 0x7f) {
            frame->rax = (uint64_t)VOSTOK_ERROR_INVALID;
        } else {
            serial_putc((char)frame->rsi);
            frame->rax = VOSTOK_OK;
        }
        return frame;
    }
    case VOSTOK_SYS_CONSOLE_GETC:
    {
        int64_t result;
        if (!console_authorized((uint32_t)frame->rdi,
                                VOSTOK_RIGHT_CONSOLE_READ, &result)) {
            frame->rax = (uint64_t)result;
            return frame;
        }
        const int character = serial_try_getc();
        if (character < 0) {
            current_process->state = PROCESS_BLOCKED_CONSOLE;
            return scheduler_schedule();
        }
        frame->rax = (uint64_t)character;
        return frame;
    }
    case VOSTOK_SYS_PROCESS_DUMP:
    {
        int64_t result;
        if (!console_authorized((uint32_t)frame->rdi,
                                VOSTOK_RIGHT_CONSOLE_INSPECT, &result)) {
            frame->rax = (uint64_t)result;
            return frame;
        }
        scheduler_dump_processes();
        frame->rax = VOSTOK_OK;
        return frame;
    }
    case VOSTOK_SYS_METRICS_DUMP:
    {
        int64_t result;
        if (!console_authorized((uint32_t)frame->rdi,
                                VOSTOK_RIGHT_CONSOLE_INSPECT, &result)) {
            frame->rax = (uint64_t)result;
            return frame;
        }
        scheduler_dump_metrics();
        frame->rax = VOSTOK_OK;
        return frame;
    }
    case VOSTOK_SYS_DEMO_READY:
    {
        int64_t result;
        frame->rax = console_authorized(
            (uint32_t)frame->rdi, VOSTOK_RIGHT_CONSOLE_INSPECT, &result)
            ? (scheduler_demo_ready() ? 1 : 0) : (uint64_t)result;
        return frame;
    }
    default:
        frame->rax = (uint64_t)VOSTOK_ERROR_INVALID;
        return frame;
    }
}

__attribute__((noreturn))
void process_system_start(void)
{
    scheduler_initialize(address_space_current());
    ASSERT(process_create(0, "sender"));
    ASSERT(process_create(1, "auditor"));
    ASSERT(process_create(2, "receiver"));
    ASSERT(process_create(3, "faulty"));
    ASSERT(process_create(4, "spinner_a"));
    ASSERT(process_create(5, "spinner_b"));
    ASSERT(process_create(6, "console"));
    ASSERT(process_create(7, "shell"));
    processes[3].expected_fault = true;

    const int64_t receiver_main = capability_install(
        &processes[2], &main_endpoint, VOSTOK_RIGHT_RECEIVE);
    const int64_t sender_main = capability_install(
        &processes[0], &main_endpoint, VOSTOK_RIGHT_SEND);
    const int64_t sender_audit = capability_install(
        &processes[0], &audit_endpoint,
        VOSTOK_RIGHT_SEND | VOSTOK_RIGHT_TRANSFER);
    const int64_t auditor_audit = capability_install(
        &processes[1], &audit_endpoint, VOSTOK_RIGHT_RECEIVE);
    ASSERT(receiver_main > 0 && sender_main > 0 && sender_audit > 0
           && auditor_audit > 0);
    processes[0].frame->rdi = (uint32_t)sender_main;
    processes[0].frame->rsi = (uint32_t)sender_audit;
    processes[1].frame->rdi = (uint32_t)auditor_audit;
    processes[2].frame->rdi = (uint32_t)receiver_main;
    const int64_t console_ipc = capability_install(
        &processes[6], &console_endpoint,
        VOSTOK_RIGHT_SEND | VOSTOK_RIGHT_RECEIVE);
    const int64_t console_device = capability_install_object(
        &processes[6], &console_object, CAPABILITY_TYPE_CONSOLE,
        VOSTOK_RIGHT_CONSOLE_READ | VOSTOK_RIGHT_CONSOLE_WRITE);
    const int64_t shell_ipc = capability_install(
        &processes[7], &console_endpoint,
        VOSTOK_RIGHT_SEND | VOSTOK_RIGHT_RECEIVE);
    const int64_t shell_inspect = capability_install_object(
        &processes[7], &console_object, CAPABILITY_TYPE_CONSOLE,
        VOSTOK_RIGHT_CONSOLE_INSPECT);
    ASSERT(console_ipc > 0 && console_device > 0
           && shell_ipc > 0 && shell_inspect > 0);
    processes[6].frame->rdi = (uint32_t)console_ipc;
    processes[6].frame->rsi = (uint32_t)console_device;
    processes[7].frame->rdi = (uint32_t)shell_ipc;
    processes[7].frame->rsi = (uint32_t)shell_inspect;

    log_info("scheduler starting eight ring-3 processes");
    process_enter(scheduler_schedule());
}

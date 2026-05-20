#include "kernel/process.h"

#include "abi/syscall.h"
#include "boot/limine_requests.h"
#include "kernel/capability.h"
#include "kernel/elf.h"
#include "kernel/ipc.h"
#include "kernel/process_internal.h"
#include "kernel/ramfs.h"
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
static uint8_t filesystem_object;
static uint8_t process_object;
static uint32_t next_pid = 1;
static bool services_started;

static bool console_authorized(uint32_t handle, uint8_t rights,
                               int64_t *result)
{
    return capability_lookup_object(current_process, handle,
                                    CAPABILITY_TYPE_CONSOLE, rights,
                                    result) != NULL;
}

extern __attribute__((noreturn))
void process_enter(struct exception_frame *frame);

void process_cleanup(struct process *process)
{
    capability_cleanup(process);
    user_image_destroy(process);
}

void process_wake_waiters(uint32_t pid, int exit_code)
{
    for (size_t index = 0; index < PROCESS_LIMIT; ++index) {
        struct process *waiter = &processes[index];
        if (waiter->state != PROCESS_BLOCKED_WAIT
            || waiter->waiting_pid != pid) continue;
        waiter->frame->rax = (uint64_t)(int64_t)exit_code;
        waiter->state = PROCESS_READY;
    }
}

static bool process_create_image(size_t index, const char *name,
                                 size_t name_length, const void *image,
                                 size_t image_size)
{
    struct process *process = &processes[index];
    memset(process, 0, sizeof(*process));
    if (name_length >= sizeof(process->name_storage))
        name_length = sizeof(process->name_storage) - 1;
    memcpy(process->name_storage, name, name_length);
    process->name_storage[name_length] = '\0';
    process->name = process->name_storage;
    process->pid = next_pid++;
    if (!address_space_create(&process->address_space)) return false;

    uint64_t entry;
    if (!user_image_load_elf_bytes(process, image, image_size, &entry)) {
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
             process->name, process->pid, (unsigned long)entry,
             (unsigned long)process->address_space.root);
    return true;
}

static bool process_create(size_t index, const char *name, size_t name_length,
                           const struct limine_file *module)
{
    return module != NULL
        && process_create_image(index, name, name_length, module->address,
                                (size_t)module->size);
}

static bool user_range_valid(const struct process *process, uint64_t address,
                             size_t length, bool writable)
{
    const uint64_t user_limit = 0x0000800000000000ULL;
    if (length == 0) return true;
    if (address >= user_limit || length > user_limit - address) return false;
    uint64_t page = address & ~(uint64_t)(PAGE_SIZE - 1);
    const uint64_t last = (address + length - 1)
                        & ~(uint64_t)(PAGE_SIZE - 1);
    for (;;) {
        if (!address_space_user_accessible(&process->address_space, page,
                                           writable))
            return false;
        if (page == last) return true;
        page += PAGE_SIZE;
    }
}

static size_t find_process_slot(void)
{
    for (size_t index = 0; index < PROCESS_LIMIT; ++index) {
        if (processes[index].state == PROCESS_UNUSED) return index;
    }
    for (size_t index = 0; index < PROCESS_LIMIT; ++index) {
        if (processes[index].state != PROCESS_DEAD) continue;
        if (processes[index].address_space.root != INVALID_PHYSICAL_ADDRESS)
            process_cleanup(&processes[index]);
        return index;
    }
    return PROCESS_LIMIT;
}

static int64_t spawn_image(const char *name, size_t length,
                           const void *image, size_t image_size,
                           uint64_t argument, uint8_t filesystem_rights)
{
    const size_t slot = find_process_slot();
    if (slot == PROCESS_LIMIT) return VOSTOK_ERROR_NO_SPACE;
    if (!process_create_image(slot, name, length, image, image_size))
        return VOSTOK_ERROR_INVALID;
    const int64_t console = capability_install(
        &processes[slot], &console_endpoint,
        VOSTOK_RIGHT_SEND | VOSTOK_RIGHT_RECEIVE);
    const int64_t filesystem = capability_install_object(
        &processes[slot], &filesystem_object, CAPABILITY_TYPE_FILESYSTEM,
        filesystem_rights);
    if (console < 0 || filesystem < 0) {
        processes[slot].state = PROCESS_DEAD;
        process_cleanup(&processes[slot]);
        return VOSTOK_ERROR_NO_SPACE;
    }
    processes[slot].frame->rdi = (uint64_t)console;
    processes[slot].frame->rsi = (uint64_t)filesystem;
    processes[slot].frame->rdx = argument;
    return processes[slot].pid;
}

static int64_t spawn_module(const char *name, size_t length,
                            uint64_t argument, uint8_t filesystem_rights)
{
    const struct limine_file *module = bootfs_find(name, length);
    return module == NULL ? VOSTOK_ERROR_NOT_FOUND
        : spawn_image(name, length, module->address, (size_t)module->size,
                      argument, filesystem_rights);
}

static bool start_services(void)
{
    if (services_started) return false;
    const size_t console_slot = find_process_slot();
    if (console_slot == PROCESS_LIMIT
        || !process_create(console_slot, "console", 7,
                           bootfs_find("console", 7))) return false;
    const int64_t console_ipc = capability_install(
        &processes[console_slot], &console_endpoint,
        VOSTOK_RIGHT_SEND | VOSTOK_RIGHT_RECEIVE);
    const int64_t console_device = capability_install_object(
        &processes[console_slot], &console_object, CAPABILITY_TYPE_CONSOLE,
        VOSTOK_RIGHT_CONSOLE_READ | VOSTOK_RIGHT_CONSOLE_WRITE);
    if (console_ipc < 0 || console_device < 0) return false;
    processes[console_slot].frame->rdi = (uint64_t)console_ipc;
    processes[console_slot].frame->rsi = (uint64_t)console_device;

    const size_t shell_slot = find_process_slot();
    if (shell_slot == PROCESS_LIMIT
        || !process_create(shell_slot, "shell", 5,
                           bootfs_find("shell", 5))) return false;
    const int64_t shell_ipc = capability_install(
        &processes[shell_slot], &console_endpoint,
        VOSTOK_RIGHT_SEND | VOSTOK_RIGHT_RECEIVE);
    const int64_t shell_inspect = capability_install_object(
        &processes[shell_slot], &console_object, CAPABILITY_TYPE_CONSOLE,
        VOSTOK_RIGHT_CONSOLE_INSPECT);
    const int64_t shell_process = capability_install_object(
        &processes[shell_slot], &process_object, CAPABILITY_TYPE_PROCESS,
        VOSTOK_RIGHT_PROCESS_SPAWN);
    const int64_t shell_filesystem = capability_install_object(
        &processes[shell_slot], &filesystem_object, CAPABILITY_TYPE_FILESYSTEM,
        VOSTOK_RIGHT_FS_READ | VOSTOK_RIGHT_FS_WRITE);
    if (shell_ipc < 0 || shell_inspect < 0 || shell_process < 0
        || shell_filesystem < 0) return false;
    processes[shell_slot].frame->rdi = (uint64_t)shell_ipc;
    processes[shell_slot].frame->rsi = (uint64_t)shell_inspect;
    processes[shell_slot].frame->rdx = (uint64_t)shell_process;
    processes[shell_slot].frame->rcx = (uint64_t)shell_filesystem;
    services_started = true;
    log_info("init: console and shell services started");
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
        process_wake_waiters(current_process->pid,
                             current_process->exit_code);
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
    case VOSTOK_SYS_FS_WRITE:
    case VOSTOK_SYS_FS_READ:
    {
        const uint8_t right = frame->rax == VOSTOK_SYS_FS_WRITE
                            ? VOSTOK_RIGHT_FS_WRITE : VOSTOK_RIGHT_FS_READ;
        int64_t result;
        if (capability_lookup_object(current_process, (uint32_t)frame->rdi,
                CAPABILITY_TYPE_FILESYSTEM, right, &result) == NULL) {
            frame->rax = (uint64_t)result;
            return frame;
        }
        const size_t path_length = (size_t)frame->rdx;
        const size_t data_length = (size_t)frame->r8;
        if (path_length >= RAMFS_PATH_LIMIT || data_length > RAMFS_FILE_SIZE
            || !user_range_valid(current_process, frame->rsi, path_length,
                                 false)
            || !user_range_valid(current_process, frame->r10,
                                 data_length,
                                 frame->rax == VOSTOK_SYS_FS_READ)) {
            frame->rax = (uint64_t)VOSTOK_ERROR_INVALID;
            return frame;
        }
        if (frame->rax == VOSTOK_SYS_FS_WRITE)
            result = ramfs_write((const char *)(uintptr_t)frame->rsi,
                                 path_length,
                                 (const uint8_t *)(uintptr_t)frame->r10,
                                 data_length);
        else
            result = ramfs_read((const char *)(uintptr_t)frame->rsi,
                                path_length,
                                (uint8_t *)(uintptr_t)frame->r10,
                                data_length);
        frame->rax = (uint64_t)result;
        return frame;
    }
    case VOSTOK_SYS_FS_LIST:
    {
        int64_t result;
        if (capability_lookup_object(current_process, (uint32_t)frame->rdi,
                CAPABILITY_TYPE_FILESYSTEM, VOSTOK_RIGHT_FS_READ,
                &result) == NULL) {
            frame->rax = (uint64_t)result;
            return frame;
        }
        if (frame->rdx > RAMFS_FILE_SIZE
            || !user_range_valid(current_process, frame->rsi,
                                 (size_t)frame->rdx, true))
            result = VOSTOK_ERROR_INVALID;
        else result = ramfs_list((char *)(uintptr_t)frame->rsi,
                                 (size_t)frame->rdx);
        frame->rax = (uint64_t)result;
        return frame;
    }
    case VOSTOK_SYS_FS_READ_ID:
    {
        int64_t result;
        if (capability_lookup_object(current_process, (uint32_t)frame->rdi,
                CAPABILITY_TYPE_FILESYSTEM, VOSTOK_RIGHT_FS_READ,
                &result) == NULL) {
            frame->rax = (uint64_t)result;
            return frame;
        }
        if (frame->r10 > RAMFS_FILE_SIZE
            || !user_range_valid(current_process, frame->rdx,
                                 (size_t)frame->r10, true))
            result = VOSTOK_ERROR_INVALID;
        else result = ramfs_read_id((size_t)frame->rsi,
                                    (uint8_t *)(uintptr_t)frame->rdx,
                                    (size_t)frame->r10);
        frame->rax = (uint64_t)result;
        return frame;
    }
    case VOSTOK_SYS_PROCESS_SPAWN:
    {
        int64_t result;
        if (capability_lookup_object(current_process, (uint32_t)frame->rdi,
                CAPABILITY_TYPE_PROCESS, VOSTOK_RIGHT_PROCESS_SPAWN,
                &result) == NULL) {
            frame->rax = (uint64_t)result;
            return frame;
        }
        const size_t length = (size_t)frame->rdx;
        const char *name = (const char *)(uintptr_t)frame->rsi;
        if (length == 0 || length >= RAMFS_PATH_LIMIT
            || !user_range_valid(current_process, frame->rsi, length,
                                 false)) {
            frame->rax = (uint64_t)VOSTOK_ERROR_INVALID;
            return frame;
        }
        if (length >= 4 && memcmp(name + length - 4, ".isk", 4) == 0) {
            result = ramfs_lookup(name, length);
            frame->rax = result < 0 ? (uint64_t)result
                : (uint64_t)spawn_module("iskra_vm", 8, (uint64_t)result,
                                         VOSTOK_RIGHT_FS_READ);
        } else {
            size_t image_size = 0;
            const uint8_t *image = ramfs_image(name, length, &image_size);
            frame->rax = image == NULL
                ? (uint64_t)spawn_module(name, length, 0,
                                         VOSTOK_RIGHT_FS_READ)
                : (uint64_t)spawn_image(name, length, image, image_size, 0,
                                        VOSTOK_RIGHT_FS_READ);
        }
        return frame;
    }
    case VOSTOK_SYS_INIT_START:
    {
        int64_t result;
        if (capability_lookup_object(current_process, (uint32_t)frame->rdi,
                CAPABILITY_TYPE_PROCESS, VOSTOK_RIGHT_PROCESS_INIT,
                &result) == NULL) frame->rax = (uint64_t)result;
        else frame->rax = start_services() ? VOSTOK_OK : VOSTOK_ERROR_BUSY;
        return frame;
    }
    case VOSTOK_SYS_PROCESS_WAIT:
    {
        int64_t result;
        if (capability_lookup_object(current_process, (uint32_t)frame->rdi,
                CAPABILITY_TYPE_PROCESS, VOSTOK_RIGHT_PROCESS_SPAWN,
                &result) == NULL) {
            frame->rax = (uint64_t)result;
            return frame;
        }
        struct process *target = NULL;
        for (size_t index = 0; index < PROCESS_LIMIT; ++index)
            if (processes[index].state != PROCESS_UNUSED
                && processes[index].pid == (uint32_t)frame->rsi)
                target = &processes[index];
        if (target == NULL) frame->rax = (uint64_t)VOSTOK_ERROR_NOT_FOUND;
        else if (target->state == PROCESS_DEAD)
            frame->rax = (uint64_t)(int64_t)target->exit_code;
        else {
            current_process->waiting_pid = target->pid;
            current_process->state = PROCESS_BLOCKED_WAIT;
            return scheduler_schedule();
        }
        return frame;
    }
    case VOSTOK_SYS_ISKRA_BUILD:
    {
        int64_t result;
        if (capability_lookup_object(current_process, (uint32_t)frame->rdi,
                CAPABILITY_TYPE_PROCESS, VOSTOK_RIGHT_PROCESS_SPAWN,
                &result) == NULL) {
            frame->rax = (uint64_t)result;
            return frame;
        }
        const size_t length = (size_t)frame->rdx;
        const char *path = (const char *)(uintptr_t)frame->rsi;
        if (length == 0 || length >= RAMFS_PATH_LIMIT
            || !user_range_valid(current_process, frame->rsi, length,
                                 false)) {
            frame->rax = (uint64_t)VOSTOK_ERROR_INVALID;
            return frame;
        }
        result = ramfs_lookup(path, length);
        frame->rax = result < 0 ? (uint64_t)result
            : (uint64_t)spawn_module("iskrac_native", 13,
                                     (uint64_t)result,
                                     VOSTOK_RIGHT_FS_READ
                                     | VOSTOK_RIGHT_FS_WRITE);
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
    ASSERT(process_create(0, "sender", 6, bootfs_find("sender", 6)));
    ASSERT(process_create(1, "auditor", 7, bootfs_find("auditor", 7)));
    ASSERT(process_create(2, "receiver", 8, bootfs_find("receiver", 8)));
    ASSERT(process_create(3, "faulty", 6, bootfs_find("faulty", 6)));
    ASSERT(process_create(4, "spinner_a", 9, bootfs_find("spinner_a", 9)));
    ASSERT(process_create(5, "spinner_b", 9, bootfs_find("spinner_b", 9)));
    ASSERT(process_create(6, "init", 4, bootfs_find("init", 4)));
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
    const int64_t init_process = capability_install_object(
        &processes[6], &process_object, CAPABILITY_TYPE_PROCESS,
        VOSTOK_RIGHT_PROCESS_INIT);
    const int64_t init_filesystem = capability_install_object(
        &processes[6], &filesystem_object, CAPABILITY_TYPE_FILESYSTEM,
        VOSTOK_RIGHT_FS_READ | VOSTOK_RIGHT_FS_WRITE);
    ASSERT(init_process > 0 && init_filesystem > 0);
    processes[6].frame->rdi = (uint64_t)init_process;
    processes[6].frame->rsi = (uint64_t)init_filesystem;

    log_info("scheduler starting boot processes and userspace init");
    process_enter(scheduler_schedule());
}

#ifndef VOSTOK_KERNEL_PROCESS_H
#define VOSTOK_KERNEL_PROCESS_H

#include "x86/exceptions.h"

__attribute__((noreturn)) void process_system_start(void);
struct exception_frame *syscall_dispatch(struct exception_frame *frame);
struct exception_frame *process_timer_tick(struct exception_frame *frame);
struct exception_frame *process_handle_fault(struct exception_frame *frame,
                                             uint64_t fault_address);
void process_serial_wake(void);
void process_wake_waiters(uint32_t pid, int exit_code);

#endif

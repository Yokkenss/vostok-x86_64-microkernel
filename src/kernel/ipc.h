#ifndef VOSTOK_KERNEL_IPC_H
#define VOSTOK_KERNEL_IPC_H

#include "kernel/process_internal.h"

struct exception_frame *ipc_send(struct exception_frame *frame);
struct exception_frame *ipc_receive(struct exception_frame *frame);
void ipc_dump_metrics(void);

#endif

#include "kernel/ipc.h"

#include "abi/syscall.h"
#include "kernel/capability.h"
#include "print.h"

#include <stdint.h>

static uint64_t message_count;
static uint64_t total_delivery_cycles;
static uint64_t minimum_delivery_cycles = UINT64_MAX;
static uint64_t maximum_delivery_cycles;

static int64_t deliver_message(struct endpoint *endpoint,
                               struct process *sender,
                               struct process *receiver, uint64_t word,
                               uint32_t transferred_capability,
                               uint8_t transferred_rights)
{
    int64_t received_handle = 0;
    if (transferred_capability != 0) {
        received_handle = capability_transfer(sender, receiver,
                                               transferred_capability,
                                               transferred_rights);
        if (received_handle < 0) {
            receiver->frame->rax = (uint64_t)received_handle;
            receiver->frame->rdx = 0;
            receiver->state = receiver == current_process
                            ? PROCESS_RUNNING : PROCESS_READY;
            sender->frame->rax = (uint64_t)received_handle;
            sender->state = sender == current_process
                          ? PROCESS_RUNNING : PROCESS_READY;
            return received_handle;
        }
    }
    receiver->frame->rax = word;
    receiver->frame->rdx = (uint64_t)received_handle;
    receiver->state = receiver == current_process
                    ? PROCESS_RUNNING : PROCESS_READY;
    sender->frame->rax = VOSTOK_OK;
    sender->state = sender == current_process ? PROCESS_RUNNING
                                              : PROCESS_READY;
    const uint64_t cycles = cpu_read_tsc() - endpoint->send_started;
    ++message_count;
    total_delivery_cycles += cycles;
    if (cycles < minimum_delivery_cycles) minimum_delivery_cycles = cycles;
    if (cycles > maximum_delivery_cycles) maximum_delivery_cycles = cycles;
    if (endpoint->trace) {
        log_info("IPC %s: %s -> %s, word=%lu, transferred rights=0x%x",
                 endpoint->name, sender->name, receiver->name,
                 (unsigned long)word, (unsigned int)transferred_rights);
    }
    return VOSTOK_OK;
}

struct exception_frame *ipc_send(struct exception_frame *frame)
{
    int64_t result;
    struct endpoint *endpoint = capability_lookup_endpoint(
        current_process, (uint32_t)frame->rdi, VOSTOK_RIGHT_SEND, &result);
    if (endpoint == NULL) {
        frame->rax = (uint64_t)result;
        return frame;
    }
    if (frame->rdx != 0) {
        int64_t transfer_result;
        (void)capability_lookup_endpoint(current_process, (uint32_t)frame->rdx,
                                         VOSTOK_RIGHT_TRANSFER,
                                         &transfer_result);
        if (transfer_result != VOSTOK_OK) {
            frame->rax = (uint64_t)transfer_result;
            return frame;
        }
    } else if (frame->r10 != 0) {
        frame->rax = (uint64_t)VOSTOK_ERROR_INVALID;
        return frame;
    }

    endpoint->send_started = cpu_read_tsc();
    if (endpoint->receiver != NULL) {
        struct process *receiver = endpoint->receiver;
        endpoint->receiver = NULL;
        result = deliver_message(endpoint, current_process, receiver,
                                 frame->rsi, (uint32_t)frame->rdx,
                                 (uint8_t)frame->r10);
        frame->rax = (uint64_t)result;
        return frame;
    }
    if (endpoint->sender != NULL) {
        frame->rax = (uint64_t)VOSTOK_ERROR_BUSY;
        return frame;
    }
    endpoint->sender = current_process;
    endpoint->word = frame->rsi;
    endpoint->transferred_capability = (uint32_t)frame->rdx;
    endpoint->transferred_rights = (uint8_t)frame->r10;
    current_process->frame = frame;
    current_process->state = PROCESS_BLOCKED_SEND;
    return scheduler_schedule();
}

struct exception_frame *ipc_receive(struct exception_frame *frame)
{
    int64_t result;
    struct endpoint *endpoint = capability_lookup_endpoint(
        current_process, (uint32_t)frame->rdi, VOSTOK_RIGHT_RECEIVE, &result);
    if (endpoint == NULL) {
        log_info("capability check rejected receive by %s: error=%ld",
                 current_process->name, (long)result);
        frame->rax = (uint64_t)result;
        return frame;
    }
    if (endpoint->sender != NULL) {
        struct process *sender = endpoint->sender;
        endpoint->sender = NULL;
        result = deliver_message(endpoint, sender, current_process,
                                 endpoint->word,
                                 endpoint->transferred_capability,
                                 endpoint->transferred_rights);
        frame->rax = result == VOSTOK_OK ? frame->rax : (uint64_t)result;
        return frame;
    }
    if (endpoint->receiver != NULL) {
        frame->rax = (uint64_t)VOSTOK_ERROR_BUSY;
        return frame;
    }
    endpoint->receiver = current_process;
    current_process->frame = frame;
    current_process->state = PROCESS_BLOCKED_RECEIVE;
    return scheduler_schedule();
}

void ipc_dump_metrics(void)
{
    log_info("IPC metrics: %lu messages, %lu average cycles, min %lu, max %lu",
             (unsigned long)message_count,
             (unsigned long)(message_count == 0 ? 0
                 : total_delivery_cycles / message_count),
             (unsigned long)(message_count == 0 ? 0 : minimum_delivery_cycles),
             (unsigned long)maximum_delivery_cycles);
}

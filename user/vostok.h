#ifndef VOSTOK_USER_API_H
#define VOSTOK_USER_API_H

#include "abi/syscall.h"

#include <stdint.h>

struct vostok_message {
    int64_t word;
    uint32_t capability;
};

static inline int64_t vostok_ipc_send(uint32_t endpoint, uint64_t word,
                                      uint32_t capability, uint8_t rights)
{
    register uint64_t argument4 __asm__("r10") = rights;
    register uint64_t result __asm__("rax") = VOSTOK_SYS_IPC_SEND;
    __asm__ volatile ("int $0x80"
                      : "+a"(result)
                      : "D"((uint64_t)endpoint), "S"(word),
                        "d"((uint64_t)capability), "r"(argument4)
                      : "memory", "cc", "r11", "rcx");
    return (int64_t)result;
}

static inline struct vostok_message vostok_ipc_receive(uint32_t endpoint)
{
    register uint64_t word __asm__("rax") = VOSTOK_SYS_IPC_RECEIVE;
    register uint64_t capability __asm__("rdx");
    __asm__ volatile ("int $0x80"
                      : "+a"(word), "=d"(capability)
                      : "D"((uint64_t)endpoint)
                      : "memory", "cc", "r11", "rcx");
    return (struct vostok_message) {
        .word = (int64_t)word,
        .capability = (uint32_t)capability,
    };
}

static inline int64_t vostok_cap_close(uint32_t capability)
{
    register uint64_t result __asm__("rax") = VOSTOK_SYS_CAP_CLOSE;
    __asm__ volatile ("int $0x80"
                      : "+a"(result)
                      : "D"((uint64_t)capability)
                      : "memory", "cc", "r11", "rcx", "rdx");
    return (int64_t)result;
}

#endif

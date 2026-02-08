#ifndef VOSTOK_ABI_SYSCALL_H
#define VOSTOK_ABI_SYSCALL_H

#include <stdint.h>

enum vostok_syscall {
    VOSTOK_SYS_EXIT = 0,
    VOSTOK_SYS_IPC_SEND = 1,
    VOSTOK_SYS_IPC_RECEIVE = 2,
    VOSTOK_SYS_CAP_CLOSE = 3,
    VOSTOK_SYS_CONSOLE_PUTC = 4,
    VOSTOK_SYS_CONSOLE_GETC = 5,
    VOSTOK_SYS_PROCESS_DUMP = 6,
    VOSTOK_SYS_METRICS_DUMP = 7,
    VOSTOK_SYS_DEMO_READY = 8,
};

enum vostok_capability_rights {
    VOSTOK_RIGHT_SEND = 1u << 0,
    VOSTOK_RIGHT_RECEIVE = 1u << 1,
    VOSTOK_RIGHT_TRANSFER = 1u << 2,
};

enum vostok_console_rights {
    VOSTOK_RIGHT_CONSOLE_READ = 1u << 3,
    VOSTOK_RIGHT_CONSOLE_WRITE = 1u << 4,
    VOSTOK_RIGHT_CONSOLE_INSPECT = 1u << 5,
};

enum vostok_result {
    VOSTOK_OK = 0,
    VOSTOK_ERROR_INVALID = -1,
    VOSTOK_ERROR_TYPE = -2,
    VOSTOK_ERROR_RIGHTS = -3,
    VOSTOK_ERROR_NO_SPACE = -4,
    VOSTOK_ERROR_BUSY = -5,
};

#endif

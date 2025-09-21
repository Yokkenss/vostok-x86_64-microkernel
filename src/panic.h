#ifndef VOSTOK_PANIC_H
#define VOSTOK_PANIC_H

__attribute__((noreturn)) void halt_forever(void);
__attribute__((noreturn)) void panic(const char *format, ...);

#define ASSERT(condition)                                                   \
    do {                                                                    \
        if (!(condition)) {                                                 \
            panic("assertion failed: %s (%s:%u)", #condition, __FILE__,    \
                  (unsigned int)__LINE__);                                  \
        }                                                                   \
    } while (0)

#endif

#ifndef VOSTOK_X86_TIMER_H
#define VOSTOK_X86_TIMER_H

#include "x86/exceptions.h"

#include <stdint.h>

void timer_init(uint32_t initial_count);
uint64_t timer_ticks(void);
void timer_end_interrupt(void);
struct exception_frame *timer_dispatch(struct exception_frame *frame);

#endif

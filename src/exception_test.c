#include "panic.h"
#include "print.h"
#include "serial.h"
#include "x86/gdt.h"
#include "x86/idt.h"

#include <stdint.h>

__attribute__((noreturn))
void kernel_main(void)
{
    serial_init();
    log_info("Vostok exception self-test");

    __asm__ volatile ("cli");
    gdt_init();
    idt_init();
    log_info("triggering an intentional page fault");

    *(volatile uint64_t *)0x00000deadbeef000ULL = 0x42;
    panic("page-fault self-test unexpectedly returned");
}

#include "boot/limine_requests.h"
#include "kernel/process.h"
#include "kernel/ramfs.h"
#include "mm/address_space.h"
#include "mm/physical.h"
#include "panic.h"
#include "print.h"
#include "serial.h"
#include "x86/gdt.h"
#include "x86/idt.h"
#include "x86/timer.h"

__attribute__((noreturn))
void kernel_main(void)
{
    serial_init();
    log_info("vostok kernel started");
    boot_info_init();

    __asm__ volatile ("cli");
    gdt_init();
    log_info("GDT and TSS loaded");
    idt_init();
    log_info("IDT loaded with 32 exception gates and ring-3 syscall gate");

    boot_dump_memory_map();
    physical_memory_init(boot_memory_map(), boot_hhdm_offset());
    const struct physical_memory_stats stats = physical_memory_stats();
    log_info("physical allocator: %lu free pages, %lu metadata pages",
             (unsigned long)stats.free_pages,
             (unsigned long)stats.metadata_pages);
    address_space_self_test();
    ramfs_initialize();
    log_info("diagnostics ready");
    timer_init(100000);
    serial_irq_init();
    process_system_start();
}

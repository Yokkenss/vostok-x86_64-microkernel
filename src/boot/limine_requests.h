#ifndef VOSTOK_BOOT_LIMINE_REQUESTS_H
#define VOSTOK_BOOT_LIMINE_REQUESTS_H

#include <limine.h>

#include <stdint.h>

void boot_info_init(void);
const struct limine_memmap_response *boot_memory_map(void);
uint64_t boot_hhdm_offset(void);
const struct limine_module_response *boot_modules(void);
void boot_dump_memory_map(void);

#endif

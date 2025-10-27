#ifndef VOSTOK_MM_PHYSICAL_H
#define VOSTOK_MM_PHYSICAL_H

#include <limine.h>

#include <stddef.h>
#include <stdint.h>

enum {
    PAGE_SIZE = 4096,
};

typedef uint64_t physical_address_t;

#define INVALID_PHYSICAL_ADDRESS UINT64_MAX

struct physical_memory_stats {
    size_t addressable_pages;
    size_t free_pages;
    size_t metadata_pages;
};

void physical_memory_init(const struct limine_memmap_response *memory_map,
                          uint64_t hhdm_offset);
physical_address_t physical_page_allocate(void);
void physical_page_free(physical_address_t address);
void *physical_to_virtual(physical_address_t address);
struct physical_memory_stats physical_memory_stats(void);

#endif

#ifndef VOSTOK_MM_ADDRESS_SPACE_H
#define VOSTOK_MM_ADDRESS_SPACE_H

#include "mm/physical.h"

#include <stdbool.h>
#include <stdint.h>

enum page_flags {
    PAGE_WRITABLE = 1ULL << 1,
    PAGE_USER = 1ULL << 2,
    PAGE_NO_EXECUTE = 1ULL << 63,
};

struct address_space {
    physical_address_t root;
};

struct address_space address_space_current(void);
bool address_space_create(struct address_space *space);
void address_space_destroy(struct address_space *space);
bool address_space_map(struct address_space *space, uint64_t virtual_address,
                       physical_address_t physical_address, uint64_t flags);
bool address_space_map_kernel(struct address_space *space,
                              uint64_t virtual_address,
                              physical_address_t physical_address,
                              uint64_t flags);
physical_address_t address_space_unmap(struct address_space *space,
                                       uint64_t virtual_address);
bool address_space_is_mapped(const struct address_space *space,
                             uint64_t virtual_address);
bool address_space_user_accessible(const struct address_space *space,
                                   uint64_t virtual_address, bool writable);
void address_space_activate(const struct address_space *space);
void address_space_self_test(void);

#endif

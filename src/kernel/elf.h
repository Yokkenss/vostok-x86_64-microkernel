#ifndef VOSTOK_KERNEL_ELF_H
#define VOSTOK_KERNEL_ELF_H

#include "kernel/process_internal.h"

#include <limine.h>

#include <stdbool.h>
#include <stdint.h>

bool user_image_load_elf(struct process *process,
                         const struct limine_file *module, uint64_t *entry);
bool user_image_map_page(struct process *process, uint64_t virtual_address,
                         uint64_t flags);
void user_image_destroy(struct process *process);

#endif

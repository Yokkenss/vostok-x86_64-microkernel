#ifndef VOSTOK_KERNEL_CAPABILITY_H
#define VOSTOK_KERNEL_CAPABILITY_H

#include "kernel/process_internal.h"

#include <stdint.h>

struct endpoint *capability_lookup_endpoint(struct process *process,
                                            uint32_t handle, uint8_t rights,
                                            int64_t *result);
void *capability_lookup_object(struct process *process, uint32_t handle,
                               uint8_t type, uint8_t rights,
                               int64_t *result);
int64_t capability_install_object(struct process *process, void *object,
                                  uint8_t type, uint8_t rights);
int64_t capability_install(struct process *process, struct endpoint *object,
                           uint8_t rights);
int64_t capability_close(struct process *process, uint32_t handle);
int64_t capability_transfer(struct process *source,
                            struct process *destination, uint32_t handle,
                            uint8_t requested_rights);
void capability_cleanup(struct process *process);

#endif

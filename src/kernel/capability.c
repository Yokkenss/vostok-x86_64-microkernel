#include "kernel/capability.h"

#include "abi/syscall.h"

#include <stddef.h>
#include <stdint.h>

static struct capability_slot *capability_lookup(struct process *process,
                                                  uint32_t handle,
                                                  uint8_t type,
                                                  uint8_t rights,
                                                  int64_t *result)
{
    const uint16_t encoded_slot = (uint16_t)handle;
    const uint16_t generation = (uint16_t)(handle >> 16);
    if (encoded_slot == 0 || encoded_slot > CAPABILITY_LIMIT) {
        *result = VOSTOK_ERROR_INVALID;
        return NULL;
    }
    struct capability_slot *slot = &process->capabilities[encoded_slot - 1];
    if (slot->generation != generation || slot->object == NULL) {
        *result = VOSTOK_ERROR_INVALID;
        return NULL;
    }
    if (type != 0 && slot->type != type) {
        *result = VOSTOK_ERROR_TYPE;
        return NULL;
    }
    if ((slot->rights & rights) != rights) {
        *result = VOSTOK_ERROR_RIGHTS;
        return NULL;
    }
    *result = VOSTOK_OK;
    return slot;
}

struct endpoint *capability_lookup_endpoint(struct process *process,
                                            uint32_t handle, uint8_t rights,
                                            int64_t *result)
{
    struct capability_slot *slot = capability_lookup(
        process, handle, CAPABILITY_TYPE_ENDPOINT, rights, result);
    return slot == NULL ? NULL : slot->object;
}

void *capability_lookup_object(struct process *process, uint32_t handle,
                               uint8_t type, uint8_t rights,
                               int64_t *result)
{
    struct capability_slot *slot = capability_lookup(
        process, handle, type, rights, result);
    return slot == NULL ? NULL : slot->object;
}

int64_t capability_install_object(struct process *process, void *object,
                                  uint8_t type, uint8_t rights)
{
    for (size_t index = 0; index < CAPABILITY_LIMIT; ++index) {
        struct capability_slot *slot = &process->capabilities[index];
        if (slot->object != NULL) continue;
        if (slot->generation == 0) slot->generation = 1;
        slot->object = object;
        slot->type = type;
        slot->rights = rights;
        return (int64_t)(((uint32_t)slot->generation << 16)
                         | (uint32_t)(index + 1));
    }
    return VOSTOK_ERROR_NO_SPACE;
}

int64_t capability_install(struct process *process, struct endpoint *object,
                           uint8_t rights)
{
    return capability_install_object(process, object,
                                     CAPABILITY_TYPE_ENDPOINT, rights);
}

int64_t capability_close(struct process *process, uint32_t handle)
{
    int64_t result;
    struct capability_slot *slot = capability_lookup(process, handle, 0, 0,
                                                      &result);
    if (slot == NULL) return result;
    slot->object = NULL;
    slot->type = 0;
    slot->rights = 0;
    if (++slot->generation == 0) slot->generation = 1;
    return VOSTOK_OK;
}

int64_t capability_transfer(struct process *source,
                            struct process *destination, uint32_t handle,
                            uint8_t requested_rights)
{
    int64_t result;
    struct capability_slot *slot = capability_lookup(
        source, handle, 0, VOSTOK_RIGHT_TRANSFER, &result);
    if (slot == NULL) return result;
    if ((requested_rights & slot->rights) != requested_rights) {
        return VOSTOK_ERROR_RIGHTS;
    }
    return capability_install_object(destination, slot->object, slot->type,
                                     requested_rights);
}

void capability_cleanup(struct process *process)
{
    for (size_t index = 0; index < CAPABILITY_LIMIT; ++index) {
        struct capability_slot *slot = &process->capabilities[index];
        if (slot->object == NULL) continue;
        slot->object = NULL;
        slot->type = 0;
        slot->rights = 0;
        if (++slot->generation == 0) slot->generation = 1;
    }
}

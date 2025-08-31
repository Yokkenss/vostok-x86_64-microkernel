#include "boot/limine_requests.h"

#include "panic.h"
#include "print.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

void boot_info_init(void)
{
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        panic("Limine does not support requested base revision 6");
    }
    if (memory_map_request.response == NULL) {
        panic("Limine did not provide a memory map");
    }
    if (hhdm_request.response == NULL) {
        panic("Limine did not provide an HHDM offset");
    }
}

const struct limine_memmap_response *boot_memory_map(void)
{
    return memory_map_request.response;
}

uint64_t boot_hhdm_offset(void)
{
    return hhdm_request.response->offset;
}

const struct limine_module_response *boot_modules(void)
{
    return module_request.response;
}

static const char *memory_type_name(uint64_t type)
{
    switch (type) {
    case LIMINE_MEMMAP_USABLE: return "usable";
    case LIMINE_MEMMAP_RESERVED: return "reserved";
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return "ACPI reclaimable";
    case LIMINE_MEMMAP_ACPI_NVS: return "ACPI NVS";
    case LIMINE_MEMMAP_BAD_MEMORY: return "bad memory";
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "bootloader reclaimable";
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return "kernel/module";
    case LIMINE_MEMMAP_FRAMEBUFFER: return "framebuffer";
    case LIMINE_MEMMAP_RESERVED_MAPPED: return "reserved mapped";
    default: return "unknown";
    }
}

void boot_dump_memory_map(void)
{
    const struct limine_memmap_response *map = boot_memory_map();
    log_info("Limine memory map: %lu entries, HHDM offset 0x%lx",
             (unsigned long)map->entry_count,
             (unsigned long)boot_hhdm_offset());

    for (uint64_t index = 0; index < map->entry_count; ++index) {
        const struct limine_memmap_entry *entry = map->entries[index];
        kprintf("  [%u] base=0x%lx length=0x%lx %s\n",
                (unsigned int)index, (unsigned long)entry->base,
                (unsigned long)entry->length, memory_type_name(entry->type));
    }
}

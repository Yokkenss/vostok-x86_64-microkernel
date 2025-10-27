#include "mm/physical.h"

#include "memory.h"
#include "mm/page_bitmap.h"
#include "panic.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static struct page_bitmap pages;
static uint64_t direct_map_offset;
static size_t bitmap_metadata_pages;
static bool initialised;

static uint64_t align_up(uint64_t value)
{
    return (value + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
}

static uint64_t align_down(uint64_t value)
{
    return value & ~(uint64_t)(PAGE_SIZE - 1);
}

static uint64_t entry_end(const struct limine_memmap_entry *entry)
{
    ASSERT(entry->base <= UINT64_MAX - entry->length);
    return entry->base + entry->length;
}

void *physical_to_virtual(physical_address_t address)
{
    ASSERT(initialised);
    ASSERT(address <= UINT64_MAX - direct_map_offset);
    return (void *)(uintptr_t)(address + direct_map_offset);
}

void physical_memory_init(const struct limine_memmap_response *memory_map,
                          uint64_t hhdm_offset)
{
    uint64_t highest_address = 0;

    ASSERT(!initialised);
    ASSERT(memory_map != NULL);
    ASSERT(memory_map->entry_count != 0);

    for (uint64_t index = 0; index < memory_map->entry_count; ++index) {
        const struct limine_memmap_entry *entry = memory_map->entries[index];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        const uint64_t end = entry_end(entry);
        if (end > highest_address) {
            highest_address = end;
        }
    }
    ASSERT(highest_address != 0);
    ASSERT(highest_address <= UINT64_MAX - (PAGE_SIZE - 1));
    const size_t page_count = (size_t)(align_up(highest_address) / PAGE_SIZE);
    const size_t bitmap_bytes = page_bitmap_storage_bytes(page_count);
    const uint64_t metadata_bytes = align_up((uint64_t)bitmap_bytes * 2);

    physical_address_t metadata_address = INVALID_PHYSICAL_ADDRESS;
    for (uint64_t index = 0; index < memory_map->entry_count; ++index) {
        const struct limine_memmap_entry *entry = memory_map->entries[index];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        const uint64_t begin = align_up(entry->base);
        const uint64_t end = align_down(entry_end(entry));
        if (end >= begin && end - begin >= metadata_bytes) {
            metadata_address = begin;
            break;
        }
    }
    if (metadata_address == INVALID_PHYSICAL_ADDRESS) {
        panic("no usable memory range can hold PMM metadata");
    }

    direct_map_offset = hhdm_offset;
    initialised = true;
    uint8_t *metadata = physical_to_virtual(metadata_address);
    page_bitmap_init(&pages, metadata, metadata + bitmap_bytes, page_count);

    for (uint64_t index = 0; index < memory_map->entry_count; ++index) {
        const struct limine_memmap_entry *entry = memory_map->entries[index];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        const uint64_t begin = align_up(entry->base);
        const uint64_t end = align_down(entry_end(entry));
        if (end > begin) {
            page_bitmap_make_managed(&pages, (size_t)(begin / PAGE_SIZE),
                                     (size_t)((end - begin) / PAGE_SIZE));
        }
    }

    bitmap_metadata_pages = (size_t)(metadata_bytes / PAGE_SIZE);
    page_bitmap_reserve(&pages, (size_t)(metadata_address / PAGE_SIZE),
                        bitmap_metadata_pages);
}

physical_address_t physical_page_allocate(void)
{
    ASSERT(initialised);
    const size_t page = page_bitmap_allocate(&pages);
    if (page == SIZE_MAX) {
        return INVALID_PHYSICAL_ADDRESS;
    }
    const physical_address_t address = (physical_address_t)page * PAGE_SIZE;
    memset(physical_to_virtual(address), 0, PAGE_SIZE);
    return address;
}

void physical_page_free(physical_address_t address)
{
    ASSERT(initialised);
    ASSERT((address & (PAGE_SIZE - 1)) == 0);
    const size_t page = (size_t)(address / PAGE_SIZE);
    const enum page_bitmap_free_result result = page_bitmap_free(&pages, page);

    if (result == PAGE_BITMAP_FREE_OUT_OF_RANGE) {
        panic("attempt to free out-of-range physical page 0x%lx",
              (unsigned long)address);
    }
    if (result == PAGE_BITMAP_FREE_NOT_MANAGED) {
        panic("attempt to free reserved physical page 0x%lx",
              (unsigned long)address);
    }
    if (result == PAGE_BITMAP_FREE_DOUBLE_FREE) {
        panic("double free of physical page 0x%lx", (unsigned long)address);
    }
}

struct physical_memory_stats physical_memory_stats(void)
{
    ASSERT(initialised);
    return (struct physical_memory_stats) {
        .addressable_pages = pages.page_count,
        .free_pages = pages.free_count,
        .metadata_pages = bitmap_metadata_pages,
    };
}

#include "mm/address_space.h"

#include "memory.h"
#include "panic.h"
#include "print.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    PAGE_PRESENT = 1ULL << 0,
    PAGE_HUGE = 1ULL << 7,
    PAGE_TABLE_ENTRIES = 512,
};

static const uint64_t PAGE_ADDRESS_MASK = 0x000ffffffffff000ULL;
static const uint64_t LOWER_HALF_LIMIT = 0x0000800000000000ULL;

static uint64_t read_cr3(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value & PAGE_ADDRESS_MASK;
}

static uint64_t *table_from_entry(uint64_t entry)
{
    return physical_to_virtual(entry & PAGE_ADDRESS_MASK);
}

static size_t table_index(uint64_t virtual_address, unsigned int shift)
{
    return (size_t)((virtual_address >> shift) & 0x1ff);
}

static uint64_t *walk_to_leaf(struct address_space *space,
                              uint64_t virtual_address, bool create,
                              uint64_t flags)
{
    uint64_t *table = physical_to_virtual(space->root);
    const unsigned int shifts[] = {39, 30, 21};

    for (size_t level = 0; level < 3; ++level) {
        uint64_t *entry = &table[table_index(virtual_address, shifts[level])];
        if ((*entry & PAGE_PRESENT) == 0) {
            if (!create) {
                return NULL;
            }
            const physical_address_t page = physical_page_allocate();
            if (page == INVALID_PHYSICAL_ADDRESS) {
                return NULL;
            }
            *entry = page | PAGE_PRESENT | PAGE_WRITABLE
                   | (flags & PAGE_USER);
        } else {
            ASSERT((*entry & PAGE_HUGE) == 0);
            if ((flags & PAGE_USER) != 0) {
                *entry |= PAGE_USER;
            }
        }
        table = table_from_entry(*entry);
    }
    return &table[table_index(virtual_address, 12)];
}

struct address_space address_space_current(void)
{
    return (struct address_space) { .root = read_cr3() };
}

bool address_space_create(struct address_space *space)
{
    const physical_address_t root = physical_page_allocate();
    if (root == INVALID_PHYSICAL_ADDRESS) {
        return false;
    }

    uint64_t *new_pml4 = physical_to_virtual(root);
    const uint64_t *current_pml4 = physical_to_virtual(read_cr3());
    memset(new_pml4, 0, PAGE_SIZE);

    /* Limine's kernel and HHDM mappings live in the upper canonical half. */
    for (size_t index = PAGE_TABLE_ENTRIES / 2;
         index < PAGE_TABLE_ENTRIES; ++index) {
        new_pml4[index] = current_pml4[index];
    }
    space->root = root;
    return true;
}

bool address_space_map(struct address_space *space, uint64_t virtual_address,
                       physical_address_t physical_address, uint64_t flags)
{
    ASSERT((virtual_address & (PAGE_SIZE - 1)) == 0);
    ASSERT((physical_address & (PAGE_SIZE - 1)) == 0);
    ASSERT(virtual_address < LOWER_HALF_LIMIT);
    ASSERT((flags & ~(PAGE_WRITABLE | PAGE_USER | PAGE_NO_EXECUTE)) == 0);

    uint64_t *leaf = walk_to_leaf(space, virtual_address, true, flags);
    if (leaf == NULL || (*leaf & PAGE_PRESENT) != 0) {
        return false;
    }
    *leaf = physical_address | PAGE_PRESENT | flags;
    return true;
}

bool address_space_map_kernel(struct address_space *space,
                              uint64_t virtual_address,
                              physical_address_t physical_address,
                              uint64_t flags)
{
    ASSERT((virtual_address & (PAGE_SIZE - 1)) == 0);
    ASSERT((physical_address & (PAGE_SIZE - 1)) == 0);
    ASSERT(virtual_address >= 0xffff800000000000ULL);
    ASSERT((flags & PAGE_USER) == 0);
    ASSERT((flags & ~(PAGE_WRITABLE | PAGE_NO_EXECUTE)) == 0);

    uint64_t *leaf = walk_to_leaf(space, virtual_address, true, flags);
    if (leaf == NULL || (*leaf & PAGE_PRESENT) != 0) {
        return false;
    }
    *leaf = physical_address | PAGE_PRESENT | flags;
    if (space->root == read_cr3()) {
        __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    }
    return true;
}

physical_address_t address_space_unmap(struct address_space *space,
                                       uint64_t virtual_address)
{
    ASSERT((virtual_address & (PAGE_SIZE - 1)) == 0);
    uint64_t *leaf = walk_to_leaf(space, virtual_address, false, 0);
    if (leaf == NULL || (*leaf & PAGE_PRESENT) == 0) {
        return INVALID_PHYSICAL_ADDRESS;
    }
    const physical_address_t physical = *leaf & PAGE_ADDRESS_MASK;
    *leaf = 0;
    if (space->root == read_cr3()) {
        __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    }
    return physical;
}

bool address_space_is_mapped(const struct address_space *space,
                             uint64_t virtual_address)
{
    struct address_space mutable_space = *space;
    uint64_t *leaf = walk_to_leaf(&mutable_space, virtual_address, false, 0);
    return leaf != NULL && (*leaf & PAGE_PRESENT) != 0;
}

bool address_space_user_accessible(const struct address_space *space,
                                   uint64_t virtual_address, bool writable)
{
    struct address_space mutable_space = *space;
    uint64_t *leaf = walk_to_leaf(&mutable_space, virtual_address, false, 0);
    if (leaf == NULL || (*leaf & (PAGE_PRESENT | PAGE_USER))
                        != (PAGE_PRESENT | PAGE_USER)) return false;
    return !writable || (*leaf & PAGE_WRITABLE) != 0;
}

void address_space_activate(const struct address_space *space)
{
    ASSERT((space->root & (PAGE_SIZE - 1)) == 0);
    __asm__ volatile ("mov %0, %%cr3" : : "r"(space->root) : "memory");
}

static void destroy_lower_table(physical_address_t table_address,
                                unsigned int level)
{
    uint64_t *table = physical_to_virtual(table_address);
    for (size_t index = 0; index < PAGE_TABLE_ENTRIES; ++index) {
        if ((table[index] & PAGE_PRESENT) == 0) {
            continue;
        }
        ASSERT((table[index] & PAGE_HUGE) == 0);
        ASSERT(level > 1);
        destroy_lower_table(table[index] & PAGE_ADDRESS_MASK, level - 1);
    }
    physical_page_free(table_address);
}

void address_space_destroy(struct address_space *space)
{
    ASSERT(space->root != read_cr3());
    uint64_t *pml4 = physical_to_virtual(space->root);

    for (size_t index = 0; index < PAGE_TABLE_ENTRIES / 2; ++index) {
        if ((pml4[index] & PAGE_PRESENT) != 0) {
            destroy_lower_table(pml4[index] & PAGE_ADDRESS_MASK, 3);
        }
    }
    physical_page_free(space->root);
    space->root = INVALID_PHYSICAL_ADDRESS;
}

void address_space_self_test(void)
{
    static const uint64_t TEST_ADDRESS = 0x00400000;
    static const uint64_t GUARD_ADDRESS = TEST_ADDRESS + PAGE_SIZE;
    static const uint64_t TEST_VALUE = 0x564f53544f4b4d4dULL;

    const struct physical_memory_stats before = physical_memory_stats();
    const struct address_space kernel_space = address_space_current();
    struct address_space test_space;
    ASSERT(!address_space_is_mapped(&kernel_space, TEST_ADDRESS));
    ASSERT(address_space_create(&test_space));

    const physical_address_t test_page = physical_page_allocate();
    ASSERT(test_page != INVALID_PHYSICAL_ADDRESS);
    ASSERT(address_space_map(&test_space, TEST_ADDRESS, test_page,
                             PAGE_WRITABLE | PAGE_USER));
    ASSERT(address_space_is_mapped(&test_space, TEST_ADDRESS));
    ASSERT(!address_space_is_mapped(&test_space, GUARD_ADDRESS));

    address_space_activate(&test_space);
    volatile uint64_t *value = (volatile uint64_t *)(uintptr_t)TEST_ADDRESS;
    *value = TEST_VALUE;
    ASSERT(*value == TEST_VALUE);
    address_space_activate(&kernel_space);
    ASSERT(!address_space_is_mapped(&kernel_space, TEST_ADDRESS));
    ASSERT(*(const uint64_t *)physical_to_virtual(test_page) == TEST_VALUE);

    ASSERT(address_space_unmap(&test_space, TEST_ADDRESS) == test_page);
    physical_page_free(test_page);
    address_space_destroy(&test_space);

    const struct physical_memory_stats after = physical_memory_stats();
    ASSERT(after.free_pages == before.free_pages);
    log_info("address-space test passed; guard page remained unmapped");
}

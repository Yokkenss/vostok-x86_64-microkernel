#include "kernel/elf.h"

#include "memory.h"
#include "mm/physical.h"
#include "panic.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    ELF_PT_LOAD = 1,
    ELF_PF_WRITE = 2,
};

struct __attribute__((packed)) elf_header {
    uint8_t identity[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
};

struct __attribute__((packed)) elf_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
};

_Static_assert(sizeof(struct elf_header) == 64, "ELF64 header size");
_Static_assert(sizeof(struct elf_program_header) == 56,
               "ELF64 program header size");

static uint64_t align_down(uint64_t value)
{
    return value & ~(uint64_t)(PAGE_SIZE - 1);
}

static bool checked_add(uint64_t left, uint64_t right, uint64_t *result)
{
    if (left > UINT64_MAX - right) return false;
    *result = left + right;
    return true;
}

bool user_image_map_page(struct process *process, uint64_t virtual_address,
                         uint64_t flags)
{
    if (process->owned_page_count == OWNED_PAGE_LIMIT) return false;
    const physical_address_t physical = physical_page_allocate();
    if (physical == INVALID_PHYSICAL_ADDRESS) return false;
    if (!address_space_map(&process->address_space, virtual_address, physical,
                           flags)) {
        physical_page_free(physical);
        return false;
    }
    process->owned_pages[process->owned_page_count++] =
        (struct owned_page) { virtual_address, physical };
    return true;
}

static physical_address_t owned_physical_at(const struct process *process,
                                             uint64_t virtual_address)
{
    for (size_t index = 0; index < process->owned_page_count; ++index) {
        if (process->owned_pages[index].virtual_address == virtual_address) {
            return process->owned_pages[index].physical_address;
        }
    }
    return INVALID_PHYSICAL_ADDRESS;
}

bool user_image_load_elf(struct process *process,
                         const struct limine_file *module, uint64_t *entry)
{
    if (module == NULL) return false;
    return user_image_load_elf_bytes(process, module->address,
                                     (size_t)module->size, entry);
}

bool user_image_load_elf_bytes(struct process *process, const void *bytes,
                               size_t image_size, uint64_t *entry)
{
    if (bytes == NULL || image_size < sizeof(struct elf_header)) return false;
    const uint8_t *image = bytes;
    const struct elf_header *header = (const void *)image;
    if (header->identity[0] != 0x7f || header->identity[1] != 'E'
        || header->identity[2] != 'L' || header->identity[3] != 'F'
        || header->identity[4] != 2 || header->identity[5] != 1
        || header->identity[6] != 1 || header->version != 1
        || header->type != 2 || header->machine != 62
        || header->program_header_size != sizeof(struct elf_program_header)) {
        return false;
    }
    const uint64_t table_size = (uint64_t)header->program_header_count
                              * sizeof(struct elf_program_header);
    if (header->program_header_offset > image_size
        || table_size > image_size - header->program_header_offset) {
        return false;
    }
    const struct elf_program_header *headers =
        (const void *)(image + header->program_header_offset);
    for (uint16_t index = 0; index < header->program_header_count; ++index) {
        const struct elf_program_header *segment = &headers[index];
        if (segment->type != ELF_PT_LOAD) continue;
        uint64_t memory_end;
        uint64_t file_end;
        if (segment->file_size > segment->memory_size
            || segment->offset > image_size
            || segment->file_size > image_size - segment->offset
            || !checked_add(segment->virtual_address, segment->memory_size,
                            &memory_end)
            || !checked_add(segment->virtual_address, segment->file_size,
                            &file_end)
            || memory_end >= 0x0000800000000000ULL) {
            return false;
        }
        for (uint64_t page = align_down(segment->virtual_address);
             page < memory_end; page += PAGE_SIZE) {
            if (address_space_is_mapped(&process->address_space, page)) {
                return false;
            }
            uint64_t flags = PAGE_USER;
            if ((segment->flags & ELF_PF_WRITE) != 0) flags |= PAGE_WRITABLE;
            if (!user_image_map_page(process, page, flags)) return false;
            const uint64_t copy_begin = page > segment->virtual_address
                                      ? page : segment->virtual_address;
            const uint64_t page_end = page + PAGE_SIZE;
            const uint64_t copy_end = page_end < file_end ? page_end : file_end;
            if (copy_end > copy_begin) {
                uint8_t *destination = physical_to_virtual(
                    owned_physical_at(process, page));
                memcpy(destination + (copy_begin - page),
                       image + segment->offset
                           + (copy_begin - segment->virtual_address),
                       (size_t)(copy_end - copy_begin));
            }
        }
    }
    if (header->entry >= 0x0000800000000000ULL
        || !address_space_is_mapped(&process->address_space,
                                    align_down(header->entry))) {
        return false;
    }
    *entry = header->entry;
    return true;
}

void user_image_destroy(struct process *process)
{
    ASSERT(process->address_space.root != address_space_current().root);
    for (size_t index = 0; index < process->owned_page_count; ++index) {
        const struct owned_page *page = &process->owned_pages[index];
        ASSERT(address_space_unmap(&process->address_space,
                                   page->virtual_address)
               == page->physical_address);
        physical_page_free(page->physical_address);
    }
    process->owned_page_count = 0;
    address_space_destroy(&process->address_space);
}

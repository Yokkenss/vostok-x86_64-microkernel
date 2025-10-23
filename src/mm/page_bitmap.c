#include "mm/page_bitmap.h"

#include <string.h>

static bool bit_test(const uint8_t *bits, size_t index)
{
    return (bits[index / 8] & (uint8_t)(1u << (index % 8))) != 0;
}

static void bit_set(uint8_t *bits, size_t index)
{
    bits[index / 8] |= (uint8_t)(1u << (index % 8));
}

static void bit_clear(uint8_t *bits, size_t index)
{
    bits[index / 8] &= (uint8_t)~(1u << (index % 8));
}

size_t page_bitmap_storage_bytes(size_t page_count)
{
    return (page_count + 7) / 8;
}

void page_bitmap_init(struct page_bitmap *bitmap, uint8_t *allocated,
                      uint8_t *managed, size_t page_count)
{
    const size_t bytes = page_bitmap_storage_bytes(page_count);
    memset(allocated, 0xff, bytes);
    memset(managed, 0, bytes);
    *bitmap = (struct page_bitmap) {
        .allocated = allocated,
        .managed = managed,
        .page_count = page_count,
    };
}

void page_bitmap_make_managed(struct page_bitmap *bitmap,
                              size_t first_page, size_t count)
{
    if (first_page >= bitmap->page_count) {
        return;
    }
    if (count > bitmap->page_count - first_page) {
        count = bitmap->page_count - first_page;
    }
    for (size_t page = first_page; page < first_page + count; ++page) {
        if (!bit_test(bitmap->managed, page)) {
            bit_set(bitmap->managed, page);
            bit_clear(bitmap->allocated, page);
            ++bitmap->free_count;
        }
    }
    if (first_page < bitmap->search_hint) {
        bitmap->search_hint = first_page;
    }
}

void page_bitmap_reserve(struct page_bitmap *bitmap,
                         size_t first_page, size_t count)
{
    if (first_page >= bitmap->page_count) {
        return;
    }
    if (count > bitmap->page_count - first_page) {
        count = bitmap->page_count - first_page;
    }
    for (size_t page = first_page; page < first_page + count; ++page) {
        if (bit_test(bitmap->managed, page)) {
            bit_clear(bitmap->managed, page);
            if (!bit_test(bitmap->allocated, page)) {
                bit_set(bitmap->allocated, page);
                --bitmap->free_count;
            }
        }
    }
}

size_t page_bitmap_allocate(struct page_bitmap *bitmap)
{
    for (size_t pass = 0; pass < 2; ++pass) {
        const size_t begin = pass == 0 ? bitmap->search_hint : 0;
        const size_t end = pass == 0 ? bitmap->page_count : bitmap->search_hint;
        for (size_t page = begin; page < end; ++page) {
            if (bit_test(bitmap->managed, page)
                && !bit_test(bitmap->allocated, page)) {
                bit_set(bitmap->allocated, page);
                --bitmap->free_count;
                bitmap->search_hint = page + 1;
                if (bitmap->search_hint == bitmap->page_count) {
                    bitmap->search_hint = 0;
                }
                return page;
            }
        }
    }
    return SIZE_MAX;
}

enum page_bitmap_free_result page_bitmap_free(struct page_bitmap *bitmap,
                                               size_t page)
{
    if (page >= bitmap->page_count) {
        return PAGE_BITMAP_FREE_OUT_OF_RANGE;
    }
    if (!bit_test(bitmap->managed, page)) {
        return PAGE_BITMAP_FREE_NOT_MANAGED;
    }
    if (!bit_test(bitmap->allocated, page)) {
        return PAGE_BITMAP_FREE_DOUBLE_FREE;
    }
    bit_clear(bitmap->allocated, page);
    ++bitmap->free_count;
    if (page < bitmap->search_hint) {
        bitmap->search_hint = page;
    }
    return PAGE_BITMAP_FREE_OK;
}

bool page_bitmap_is_managed(const struct page_bitmap *bitmap, size_t page)
{
    return page < bitmap->page_count && bit_test(bitmap->managed, page);
}

bool page_bitmap_is_allocated(const struct page_bitmap *bitmap, size_t page)
{
    return page >= bitmap->page_count || bit_test(bitmap->allocated, page);
}

#ifndef VOSTOK_MM_PAGE_BITMAP_H
#define VOSTOK_MM_PAGE_BITMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum page_bitmap_free_result {
    PAGE_BITMAP_FREE_OK,
    PAGE_BITMAP_FREE_OUT_OF_RANGE,
    PAGE_BITMAP_FREE_NOT_MANAGED,
    PAGE_BITMAP_FREE_DOUBLE_FREE,
};

struct page_bitmap {
    uint8_t *allocated;
    uint8_t *managed;
    size_t page_count;
    size_t free_count;
    size_t search_hint;
};

size_t page_bitmap_storage_bytes(size_t page_count);
void page_bitmap_init(struct page_bitmap *bitmap, uint8_t *allocated,
                      uint8_t *managed, size_t page_count);
void page_bitmap_make_managed(struct page_bitmap *bitmap,
                              size_t first_page, size_t page_count);
void page_bitmap_reserve(struct page_bitmap *bitmap,
                         size_t first_page, size_t page_count);
size_t page_bitmap_allocate(struct page_bitmap *bitmap);
enum page_bitmap_free_result page_bitmap_free(struct page_bitmap *bitmap,
                                               size_t page);
bool page_bitmap_is_managed(const struct page_bitmap *bitmap, size_t page);
bool page_bitmap_is_allocated(const struct page_bitmap *bitmap, size_t page);

#endif

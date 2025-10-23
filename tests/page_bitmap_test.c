#include "mm/page_bitmap.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint8_t allocated[3];
    uint8_t managed[3];
    struct page_bitmap bitmap;

    page_bitmap_init(&bitmap, allocated, managed, 20);
    assert(bitmap.free_count == 0);

    page_bitmap_make_managed(&bitmap, 2, 10);
    assert(bitmap.free_count == 10);
    assert(page_bitmap_is_managed(&bitmap, 2));
    assert(!page_bitmap_is_allocated(&bitmap, 2));

    page_bitmap_reserve(&bitmap, 4, 2);
    assert(bitmap.free_count == 8);
    assert(!page_bitmap_is_managed(&bitmap, 4));

    const size_t first = page_bitmap_allocate(&bitmap);
    const size_t second = page_bitmap_allocate(&bitmap);
    assert(first == 2);
    assert(second == 3);
    assert(bitmap.free_count == 6);

    assert(page_bitmap_free(&bitmap, first) == PAGE_BITMAP_FREE_OK);
    assert(page_bitmap_free(&bitmap, first) == PAGE_BITMAP_FREE_DOUBLE_FREE);
    assert(page_bitmap_free(&bitmap, 4) == PAGE_BITMAP_FREE_NOT_MANAGED);
    assert(page_bitmap_free(&bitmap, 99) == PAGE_BITMAP_FREE_OUT_OF_RANGE);
    assert(page_bitmap_allocate(&bitmap) == first);

    printf("page bitmap tests passed\n");
    return 0;
}
